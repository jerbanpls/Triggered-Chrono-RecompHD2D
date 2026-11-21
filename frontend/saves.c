/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Save RAM on disk: see saves.h. */
#include "saves.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL.h>

#include "bus.h"
#include "settings.h"
#include "platform.h"   /* port: Windows: rename that replaces, link, O_BINARY */

#define PATHLEN 4096

static char path[PATHLEN];   /* "" while saving is off */
static uint8_t on_disk[CT_SRAM_SIZE];   /* what the file holds */
static uint8_t prev[CT_SRAM_SIZE];      /* the SRAM at the previous frame edge */
static Uint32 last_write, dirty_since;
static int have_write, dirty, exit_hooked;
static const char *dump_path;
static long cur_frame = -1;   /* for the log: the frame a timed write follows */

static void at_exit(void);

/* port: the clock the backups' names and pruning use (saves_set_clock). */
static time_t (*clock_fn)(void);
static time_t now_time(void)
{
    return clock_fn ? clock_fn() : time(NULL);
}
static int backups_on = 1;   /* port: saves_backup_auto */
static void backup_auto(void);

static void hook_exit(void)
{
    if (!exit_hooked) {
        exit_hooked = 1;
        atexit(at_exit);
    }
}

/* Move a wrong-size file out of the way: FILE.bad, else FILE.bad.N. */
static void set_aside(void)
{
    char bad[PATHLEN + 16];
    for (int n = 0; n < 100; n++) {
        if (n)
            snprintf(bad, sizeof bad, "%s.bad.%d", path, n);
        else
            snprintf(bad, sizeof bad, "%s.bad", path);
        if (access(bad, F_OK) && errno == ENOENT) {
            if (rename(path, bad))
                break;
            fprintf(stderr, "ct_sdl: save: WARNING: %s is not an %u-byte save file; kept it "
                            "as %s and started without a save\n", path, CT_SRAM_SIZE, bad);
            return;
        }
    }
    fprintf(stderr, "ct_sdl: save: WARNING: %s is not an %u-byte save file and cannot be "
                    "moved aside (%s): saving is off\n", path, CT_SRAM_SIZE, strerror(errno));
    path[0] = 0;
}

int saves_open(const char *file, const char *game_id)
{
    if (file) {
        snprintf(path, sizeof path, "%s", file);
    } else {
        char *dir = SDL_GetPrefPath("ct-recomp", game_id);
        if (!dir) {
            fprintf(stderr, "ct_sdl: save: no preferences folder (%s): saving is off\n",
                    SDL_GetError());
            return 1;
        }
        snprintf(path, sizeof path, "%s%s.srm", dir, game_id);
        SDL_free(dir);
    }
    uint8_t *sram = bus_sram();
    memcpy(on_disk, sram, CT_SRAM_SIZE);   /* no file: the power-on SRAM counts as saved */
    FILE *f = fopen(path, "rb");
    if (!f && errno != ENOENT) {
        fprintf(stderr, "ct_sdl: save: cannot read %s (%s): saving is off\n", path,
                strerror(errno));
        path[0] = 0;
        return 1;
    }
    if (f) {
        size_t n = fread(on_disk, 1, CT_SRAM_SIZE, f);
        int extra = fgetc(f), err = ferror(f);
        fclose(f);
        if (err) {
            fprintf(stderr, "ct_sdl: save: read error on %s: saving is off\n", path);
            path[0] = 0;
            return 1;
        }
        if (n != CT_SRAM_SIZE || extra != EOF) {
            memcpy(on_disk, sram, CT_SRAM_SIZE);
            set_aside();
            if (!path[0])
                return 1;
        } else {
            memcpy(sram, on_disk, CT_SRAM_SIZE);
            fprintf(stderr, "ct_sdl: save: loaded %s\n", path);
        }
    } else {
        fprintf(stderr, "ct_sdl: save: %s (new)\n", path);
    }
    memcpy(prev, sram, CT_SRAM_SIZE);
    hook_exit();
    backup_auto();   /* port: the save as the last run left it */
    return 0;
}

static int fsync_dir(const char *file)
{
    char dir[PATHLEN];
    snprintf(dir, sizeof dir, "%s", file);
    char *slash = strrchr(dir, '/');
    if (slash)
        slash[slash == dir] = 0;   /* "/x" -> "/" */
    else
        snprintf(dir, sizeof dir, ".");
    int fd = open(dir, O_RDONLY);
    if (fd < 0)
        return -1;
    int r = fsync(fd);
    close(fd);
    return r;
}

/* FILE.tmp, fsync, FILE -> FILE.bak (a hard link, else a copy), FILE.tmp
   -> FILE. 0 on success. port: any file (export), and the .bak optional. */
static int write_atomic_to(const char *target, const uint8_t *data, int keep_bak)
{
    char tmp[PATHLEN + 8], bak[PATHLEN + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", target);
    snprintf(bak, sizeof bak, "%s.bak", target);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0644);
    if (fd < 0)
        return -1;
    size_t done = 0;
    while (done < CT_SRAM_SIZE) {
        ssize_t w = write(fd, data + done, CT_SRAM_SIZE - done);
        if (w < 0 && errno == EINTR)
            continue;
        if (w <= 0)
            break;
        done += (size_t)w;
    }
    if (done != CT_SRAM_SIZE || fsync(fd)) {
        close(fd);
        remove(tmp);
        return -1;
    }
    if (close(fd)) {
        remove(tmp);
        return -1;
    }
    if (keep_bak && !access(target, F_OK)) {
        remove(bak);
        if (link(target, bak)) {
            /* No hard links here: copy it. */
            FILE *in = fopen(target, "rb"), *out = in ? fopen(bak, "wb") : NULL;
            uint8_t buf[CT_SRAM_SIZE];
            size_t n = in ? fread(buf, 1, sizeof buf, in) : 0;
            if (out)
                fwrite(buf, 1, n, out);
            if (out)
                fclose(out);
            if (in)
                fclose(in);
        }
    }
    if (rename(tmp, target)) {
        remove(tmp);
        return -1;
    }
    fsync_dir(target);
    return 0;
}

static int write_atomic(const uint8_t *data)
{
    return write_atomic_to(path, data, 1);
}

static int save_now(const uint8_t *sram)
{
    backup_auto();   /* port: the file as it is, before it is replaced */
    if (write_atomic(sram)) {
        fprintf(stderr, "ct_sdl: save: cannot write %s (%s)\n", path, strerror(errno));
        return 1;   /* on_disk unchanged: retried after the interval */
    }
    memcpy(on_disk, sram, CT_SRAM_SIZE);
    if (cur_frame >= 0)
        fprintf(stderr, "ct_sdl: save: wrote %s (after frame %ld)\n", path, cur_frame);
    else
        fprintf(stderr, "ct_sdl: save: wrote %s (at exit)\n", path);
    return 0;
}

#define MAX_POKES 16
static struct { long frame; unsigned addr, val; } pokes[MAX_POKES];
static int n_pokes;

int saves_poke(const char *spec)
{
    long f;
    unsigned a, v;
    char end;
    if (n_pokes == MAX_POKES || sscanf(spec, "%ld:%x=%x%c", &f, &a, &v, &end) != 3 ||
        a >= CT_SRAM_SIZE || v > 0xFF)
        return -1;
    pokes[n_pokes].frame = f;
    pokes[n_pokes].addr = a;
    pokes[n_pokes].val = v;
    n_pokes++;
    return 0;
}

void saves_frame(long f)
{
    for (int k = 0; k < n_pokes; k++)
        if (pokes[k].frame == f)
            bus_sram()[pokes[k].addr] = (uint8_t)pokes[k].val;
    if (!path[0])
        return;
    const uint8_t *sram = bus_sram();
    int stable = !memcmp(sram, prev, CT_SRAM_SIZE);
    if (!stable)
        memcpy(prev, sram, CT_SRAM_SIZE);
    if (!memcmp(sram, on_disk, CT_SRAM_SIZE)) {
        dirty = 0;
        return;
    }
    Uint32 now = SDL_GetTicks();
    if (!dirty) {
        dirty = 1;
        dirty_since = now;
    }
    /* Not in the middle of the game's own write, unless it never settles. */
    if (!stable && now - dirty_since < 2 * SAVES_INTERVAL_MS)
        return;
    if (have_write && now - last_write < SAVES_INTERVAL_MS)
        return;
    last_write = now;
    have_write = 1;
    cur_frame = f;
    if (!save_now(sram))
        dirty = 0;
    cur_frame = -1;
}

void saves_flush(void)
{
    if (path[0] && memcmp(bus_sram(), on_disk, CT_SRAM_SIZE))
        save_now(bus_sram());
}

void saves_dump_at_exit(const char *file)
{
    dump_path = file;
    hook_exit();
}

static void at_exit(void)
{
    saves_flush();
    if (!dump_path)
        return;
    FILE *f = fopen(dump_path, "wb");
    int ok = f && fwrite(bus_sram(), 1, CT_SRAM_SIZE, f) == CT_SRAM_SIZE;
    if (f && fclose(f))
        ok = 0;
    fprintf(stderr, "ct_sdl: SRAM %s %s\n", ok ? "dumped to" : "cannot be dumped to", dump_path);
}

/* ---- port: save states and import/export (saves.h) ---- */

void saves_after_state_load(void)
{
    if (!path[0])
        return;   /* no save file: the state's save RAM stays */
    memcpy(bus_sram(), on_disk, CT_SRAM_SIZE);
    memcpy(prev, on_disk, CT_SRAM_SIZE);
    dirty = 0;
}

/* The save file's path, as saves_open finds it. 0, or -1 (printed). */
static int find_path(const char *file, const char *game_id)
{
    if (file) {
        snprintf(path, sizeof path, "%s", file);
        return 0;
    }
    char *dir = SDL_GetPrefPath("ct-recomp", game_id);
    if (!dir) {
        fprintf(stderr, "ct_sdl: save: no preferences folder (%s)\n", SDL_GetError());
        return -1;
    }
    snprintf(path, sizeof path, "%s%s.srm", dir, game_id);
    SDL_free(dir);
    return 0;
}

/* Exactly CT_SRAM_SIZE bytes of file into buf. 0, or -1 (printed). */
static int read_srm(const char *file, uint8_t *buf)
{
    FILE *f = fopen(file, "rb");
    if (!f) {
        fprintf(stderr, "ct_sdl: cannot read %s (%s)\n", file, strerror(errno));
        return -1;
    }
    size_t n = fread(buf, 1, CT_SRAM_SIZE, f);
    int extra = fgetc(f), err = ferror(f);
    fclose(f);
    if (err || n != CT_SRAM_SIZE || extra != EOF) {
        fprintf(stderr, "ct_sdl: %s is not a %u-byte (%u KiB) save RAM file\n", file,
                CT_SRAM_SIZE, CT_SRAM_SIZE / 1024);
        return -1;
    }
    return 0;
}

/* Install data as the save file: the save it replaces is kept as
   FILE.<tag>YYYYmmdd-HHMMSS, then the new one is written atomically (as
   above, with FILE.bak). The path is the global one. 0, or 1 (printed).
   The --import-srm path, and the live restore of saves_backup_restore. */
static int install_srm(const uint8_t *data, const char *tag)
{
    uint8_t old[CT_SRAM_SIZE];
    if (!access(path, F_OK)) {   /* the save it replaces, kept under a new name */
        if (read_srm(path, old))
            return 1;
        char keep[PATHLEN + 64];
        struct tm tm;
        time_t t = now_time();
        localtime_r(&t, &tm);
        size_t n = (size_t)snprintf(keep, sizeof keep, "%s.%s", path, tag);
        strftime(keep + n, sizeof keep - n, "%Y%m%d-%H%M%S", &tm);
        char saved[PATHLEN];
        snprintf(saved, sizeof saved, "%s", path);
        if (write_atomic_to(keep, old, 0)) {
            fprintf(stderr, "ct_sdl: cannot back up %s to %s (%s): nothing replaced\n", saved,
                    keep, strerror(errno));
            return 1;
        }
        fprintf(stderr, "ct_sdl: the old save is kept as %s\n", keep);
    }
    if (write_atomic(data)) {
        fprintf(stderr, "ct_sdl: cannot write %s (%s)\n", path, strerror(errno));
        return 1;
    }
    return 0;
}

int saves_import(const char *from, const char *file, const char *game_id)
{
    uint8_t data[CT_SRAM_SIZE];
    if (read_srm(from, data) || find_path(file, game_id))
        return 1;
    if (install_srm(data, "before-import-"))
        return 1;
    fprintf(stderr, "ct_sdl: imported %s as %s\n", from, path);
    path[0] = 0;
    return 0;
}

int saves_export(const char *to, const char *file, const char *game_id)
{
    uint8_t data[CT_SRAM_SIZE];
    if (find_path(file, game_id))
        return 1;
    char from[PATHLEN];
    snprintf(from, sizeof from, "%s", path);
    path[0] = 0;
    if (read_srm(from, data))
        return 1;
    if (write_atomic_to(to, data, 0)) {
        fprintf(stderr, "ct_sdl: cannot write %s (%s)\n", to, strerror(errno));
        return 1;
    }
    fprintf(stderr, "ct_sdl: exported %s to %s\n", from, to);
    return 0;
}

/* ---- port: rotating backups, restore, reset (saves.h) ---- */

void saves_set_clock(time_t (*fn)(void))
{
    clock_fn = fn;
}

void saves_backup_auto(int on)
{
    backups_on = on != 0;
}

#define BACKUP_TAG ".backup-"
#define STAMP_LEN 15   /* YYYYmmdd-HHMMSS */
#define BACKUP_MAX 512   /* what one folder listing keeps */

static void stamp_of(time_t t, char *out, size_t n)
{
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(out, n, "%Y%m%d-%H%M%S", &tm);
}

static int stamp_ok(const char *s)
{
    for (int k = 0; k < STAMP_LEN; k++)
        if (k == 8 ? s[k] != '-' : (s[k] < '0' || s[k] > '9'))
            return 0;
    return s[STAMP_LEN] == 0;
}

/* Days since 1970-01-01 of a stamp's date (Howard Hinnant's civil-days). */
static long stamp_day(const char *s)
{
    long y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
    long m = (s[4] - '0') * 10 + (s[5] - '0'), d = (s[6] - '0') * 10 + (s[7] - '0');
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static void backup_name(char *out, size_t n, const char *stamp)
{
    snprintf(out, n, "%s" BACKUP_TAG "%s", path, stamp);
}

static int stamp_cmp_desc(const void *a, const void *b)
{
    return strcmp((const char *)b, (const char *)a);
}

/* The stamps of FILE.backup-* next to the save file, newest first.
   Returns how many (at most BACKUP_MAX). */
static int scan_backups(char stamps[][STAMP_LEN + 1])
{
    char dir[PATHLEN];
    snprintf(dir, sizeof dir, "%s", path);
    char *slash = strrchr(dir, '/');
    const char *base = slash ? slash + 1 : dir;
    if (slash)
        slash[slash == dir] = 0;
    else
        snprintf(dir, sizeof dir, ".");
    char prefix[PATHLEN + 16];
    /* base points into dir: copy it before dir is reused */
    snprintf(prefix, sizeof prefix, "%s" BACKUP_TAG, base);
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int n = 0;
    size_t plen = strlen(prefix);
    struct dirent *e;
    while ((e = readdir(d)) && n < BACKUP_MAX)
        if (!strncmp(e->d_name, prefix, plen) && stamp_ok(e->d_name + plen)) {
            memcpy(stamps[n], e->d_name + plen, STAMP_LEN + 1);
            n++;
        }
    closedir(d);
    qsort(stamps, (size_t)n, STAMP_LEN + 1, stamp_cmp_desc);
    return n;
}

/* Keep the newest BACKUPS_KEEP_LAST, plus the newest of each of the last
   BACKUPS_KEEP_DAYS days (today included) that has none among them. */
static void prune_backups(void)
{
    static char st[BACKUP_MAX][STAMP_LEN + 1];
    int n = scan_backups(st);
    char today[STAMP_LEN + 1];
    stamp_of(now_time(), today, sizeof today);
    long t0 = stamp_day(today);
    int have[BACKUPS_KEEP_DAYS] = {0};
    for (int k = 0; k < n; k++) {
        long age = t0 - stamp_day(st[k]);
        int in_days = age >= 0 && age < BACKUPS_KEEP_DAYS;
        int keep = k < BACKUPS_KEEP_LAST;
        if (!keep && in_days && !have[age])
            keep = 1;
        if (keep && in_days)
            have[age] = 1;
        if (!keep) {
            char name[PATHLEN + 32];
            backup_name(name, sizeof name, st[k]);
            remove(name);
        }
    }
}

/* Write data as a new backup unless the newest one is the same. 0, or -1. */
static int backup_data(const uint8_t *data)
{
    static char st[BACKUP_MAX][STAMP_LEN + 1];
    int n = scan_backups(st);
    char name[PATHLEN + 32];
    if (n) {
        uint8_t last[CT_SRAM_SIZE];
        backup_name(name, sizeof name, st[0]);
        FILE *f = fopen(name, "rb");
        size_t got = f ? fread(last, 1, CT_SRAM_SIZE, f) : 0;
        int extra = f ? fgetc(f) : 0;
        if (f)
            fclose(f);
        if (got == CT_SRAM_SIZE && extra == EOF && !memcmp(last, data, CT_SRAM_SIZE))
            return 0;   /* already backed up */
    }
    time_t t = now_time();
    char stamp[STAMP_LEN + 1];
    for (int tries = 0; tries < 120; tries++, t++) {   /* a name in use: the next second */
        stamp_of(t, stamp, sizeof stamp);
        backup_name(name, sizeof name, stamp);
        if (access(name, F_OK))
            break;
    }
    if (write_atomic_to(name, data, 0)) {
        fprintf(stderr, "ct_sdl: save: cannot write the backup %s (%s)\n", name, strerror(errno));
        return -1;
    }
    fprintf(stderr, "ct_sdl: save: backup %s\n", name);
    prune_backups();
    return 0;
}

/* Before the save file is replaced (and at start): copy it as it is. */
static void backup_auto(void)
{
    if (!backups_on || !path[0] || access(path, F_OK))
        return;
    uint8_t data[CT_SRAM_SIZE];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(data, 1, CT_SRAM_SIZE, f) : 0;
    int extra = f ? fgetc(f) : 0;
    if (f)
        fclose(f);
    if (n == CT_SRAM_SIZE && extra == EOF)
        backup_data(data);
}

static Uint32 backup_status_at;   /* settings rows: when the list was last made */

int saves_backup_now(void)
{
    if (!path[0])
        return -1;
    saves_flush();
    uint8_t data[CT_SRAM_SIZE];
    if (read_srm(path, data))
        return -1;
    backup_status_at = 0;
    return backup_data(data);
}

int saves_backup_list(saves_backup *out, int max)
{
    if (!path[0] || max <= 0)
        return 0;
    static char st[BACKUP_MAX][STAMP_LEN + 1];
    int n = scan_backups(st);
    if (n > max)
        n = max;
    for (int k = 0; k < n; k++) {
        const char *s = st[k];
        memcpy(out[k].stamp, s, STAMP_LEN + 1);
        snprintf(out[k].label, sizeof out[k].label, "%.4s-%.2s-%.2s %.2s:%.2s:%.2s", s, s + 4,
                 s + 6, s + 9, s + 11, s + 13);
        struct tm tm = {0};
        tm.tm_year = atoi((char[]){s[0], s[1], s[2], s[3], 0}) - 1900;
        tm.tm_mon = atoi((char[]){s[4], s[5], 0}) - 1;
        tm.tm_mday = atoi((char[]){s[6], s[7], 0});
        tm.tm_hour = atoi((char[]){s[9], s[10], 0});
        tm.tm_min = atoi((char[]){s[11], s[12], 0});
        tm.tm_sec = atoi((char[]){s[13], s[14], 0});
        tm.tm_isdst = -1;
        out[k].when = (long long)mktime(&tm);
    }
    return n;
}

int saves_backup_restore(int index)
{
    saves_backup list[BACKUPS_MAX_LIST];
    int n = saves_backup_list(list, BACKUPS_MAX_LIST);
    if (index < 0 || index >= n) {
        fprintf(stderr, "ct_sdl: save: no backup number %d to restore\n", index);
        return -1;
    }
    char name[PATHLEN + 32];
    backup_name(name, sizeof name, list[index].stamp);
    uint8_t data[CT_SRAM_SIZE];
    if (read_srm(name, data)) {   /* not exactly 8,192 bytes: refused, nothing touched */
        fprintf(stderr, "ct_sdl: save: not restoring %s\n", name);
        return -1;
    }
    saves_flush();   /* the save as it is now is what gets kept */
    if (install_srm(data, "before-restore-"))
        return -1;
    memcpy(bus_sram(), data, CT_SRAM_SIZE);   /* the running game's save RAM too */
    memcpy(on_disk, data, CT_SRAM_SIZE);
    memcpy(prev, data, CT_SRAM_SIZE);
    dirty = 0;
    backup_status_at = 0;
    fprintf(stderr, "ct_sdl: save: restored %s\n", name);
    return 0;
}

/* The user's Reset: the main loop presses the console's reset button at
   the next frame (main.c's reset hook asks game_reset_take). */
static int reset_pending;

int game_reset_request(void)
{
    reset_pending = 1;
    return 1;
}

int game_reset_take(void)
{
    int r = reset_pending;
    reset_pending = 0;
    return r;
}

/* ---- the settings rows (F10 > Save states) ---- */

static char status_line[96];
static saves_backup row_list[SAVES_RESTORE_ROWS];
static int row_count;

static void rows_refresh(void)
{
    Uint32 now = SDL_GetTicks();
    if (backup_status_at && now - backup_status_at < 1000)
        return;
    backup_status_at = now ? now : 1;
    row_count = saves_backup_list(row_list, SAVES_RESTORE_ROWS);
}

static int row_saving_on(void *u)
{
    (void)u;
    return path[0] != 0;
}

static void row_backup(void *u)
{
    (void)u;
    int r = saves_backup_now();
    snprintf(status_line, sizeof status_line, "%s", r ? "Could not back up" : "Backed up");
}

static const char *row_backup_text(void *u, char *buf, size_t n)
{
    (void)u;
    (void)buf;
    (void)n;
    return status_line;
}

static int row_restore_on(void *u)
{
    rows_refresh();
    return path[0] && (int)(intptr_t)u < row_count;
}

static const char *row_restore_text(void *u, char *buf, size_t n)
{
    rows_refresh();
    int k = (int)(intptr_t)u;
    if (k >= row_count)
        return "-";
    snprintf(buf, n, "%s", row_list[k].label);
    return buf;
}

static void row_restore(void *u)
{
    int r = saves_backup_restore((int)(intptr_t)u);
    snprintf(status_line, sizeof status_line, "%s",
             r ? "Not restored" : "Restored: reset or load from the title screen");
}

static void row_reset(void *u)
{
    (void)u;
    game_reset_request();
}

void saves_register_settings(void)
{
    static const char *const labels[SAVES_RESTORE_ROWS] = {
        "Restore backup 1", "Restore backup 2", "Restore backup 3",
        "Restore backup 4", "Restore backup 5", "Restore backup 6"};
    settings_register(&(setting){.label = "Save file backups", .category = "Save states",
                                 .type = SETTING_INFO, .flags = SETTING_HEADING});
    settings_register(&(setting){
        .label = "Back up now", .category = "Save states", .type = SETTING_ACTION,
        .run = row_backup, .enabled = row_saving_on, .text = row_backup_text,
        .help = "A copy of the game's save file (the last 5 and one a day for a week are kept "
                "automatically, beside the save file)."});
    for (int k = 0; k < SAVES_RESTORE_ROWS; k++)
        settings_register(&(setting){
            .label = labels[k], .category = "Save states", .type = SETTING_ACTION,
            .run = row_restore, .enabled = row_restore_on, .text = row_restore_text,
            .user = (void *)(intptr_t)k, .flags = SETTING_CONFIRM,
            .help = "Press twice: the save file becomes this copy; the one it replaces is kept "
                    "as .before-restore-DATE-TIME. Then reset, or load from the title screen."});
    settings_register(&(setting){.label = "", .category = "Save states", .type = SETTING_INFO,
                                 .flags = SETTING_HEADING});
    settings_register(&(setting){
        .label = "Reset the game", .category = "Save states", .type = SETTING_ACTION,
        .run = row_reset, .flags = SETTING_CONFIRM | SETTING_CLOSE,
        .help = "Press the console's reset button: back to the title screen. Press twice. "
                "Unsaved progress is lost."});
}
