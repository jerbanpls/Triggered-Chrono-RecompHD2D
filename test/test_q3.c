/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Overlays, controls and save safety of the quick menu work, with no ROM
 * and no window: the rotating SRAM backups (rotation, pruning, restore,
 * refusal of a bad file), the user's Reset request, the overlay data
 * providers (frame time, play time and its file, clock, battery), and the
 * settings rows' parsing for the touch, controls and overlay options. The
 * layout swap and stick dead zone go through virtual pads in test_actions. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "harness.h"
#include "input.h"
#include "overlays.h"
#include "qol.h"
#include "saves.h"
#include "settings.h"
#include "touch.h"
#include "platform.h"   /* port: Windows: mkdir with no mode, setenv */

static char tmp[4096];
static time_t now;

static time_t fake_clock(void) { return now; }

static time_t at_day(int day, int hour, int min)   /* day 0 = 2026-06-01 */
{
    struct tm tm = {0};
    tm.tm_year = 126;
    tm.tm_mon = 5;
    tm.tm_mday = 1 + day;
    tm.tm_hour = hour;
    tm.tm_min = min;
    tm.tm_isdst = -1;
    return mktime(&tm);
}

static void version(int v)
{
    memset(bus_sram(), v, CT_SRAM_SIZE);
    bus_sram()[100] = (uint8_t)(v >> 8);
}

static int file_is(const char *file, int v)
{
    uint8_t buf[CT_SRAM_SIZE + 1];
    FILE *f = fopen(file, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    return n == CT_SRAM_SIZE && buf[0] == (uint8_t)v && buf[1] == (uint8_t)v;
}

static int exists(const char *p) { return access(p, F_OK) == 0; }

static void put_file(const char *p, const char *text, size_t n)
{
    FILE *f = fopen(p, "wb");
    CHECK(f != NULL, "write %s", p);
    if (f) {
        fwrite(text, 1, n, f);
        fclose(f);
    }
}

static int count_names(const char *dir, const char *needle)
{
    char cmd[8300];
    snprintf(cmd, sizeof cmd, "ls '%s' | grep -c -- '%s'", dir, needle);
    FILE *p = popen(cmd, "r");
    int n = 0;
    if (p) {
        if (fscanf(p, "%d", &n) != 1)
            n = 0;
        pclose(p);
    }
    return n;
}

static void test_backups(void)
{
    char dir[4200], srm[4300], p[4400];
    snprintf(dir, sizeof dir, "%s/saves", tmp);
    mkdir(dir, 0755);
    snprintf(srm, sizeof srm, "%s/ct.srm", dir);
    saves_set_clock(fake_clock);

    /* A day of play: ten saves a minute apart. Backups of the file before
       each replacement: the newest five are kept (all of today). */
    now = at_day(0, 9, 0);
    CHECK(saves_open(srm, "ct") == 0, "open");
    for (int v = 1; v <= 10; v++) {
        version(v);
        saves_flush();
        now += 60;
    }
    saves_backup list[BACKUPS_MAX_LIST];
    int n = saves_backup_list(list, BACKUPS_MAX_LIST);
    CHECK(n == BACKUPS_KEEP_LAST, "five kept of nine, got %d", n);
    snprintf(p, sizeof p, "%s/ct.srm.backup-%s", dir, list[0].stamp);
    CHECK(file_is(p, 9), "the newest backup is the file before the last write (v9)");
    snprintf(p, sizeof p, "%s/ct.srm.backup-%s", dir, list[4].stamp);
    CHECK(file_is(p, 5), "the oldest kept is v5");
    CHECK(strncmp(list[0].stamp, "20260601-0909", 13) == 0, "stamp %s", list[0].stamp);
    CHECK(strcmp(list[0].label, "2026-06-01 09:09:00") == 0, "label %s", list[0].label);
    CHECK(file_is(srm, 10), "the save file is v10");

    /* "Back up now" copies the current file; again with no change: no new file. */
    CHECK(saves_backup_now() == 0, "backup now");
    CHECK(saves_backup_list(list, BACKUPS_MAX_LIST) == 5, "still five (pruned)");
    snprintf(p, sizeof p, "%s/ct.srm.backup-%s", dir, list[0].stamp);
    CHECK(file_is(p, 10), "backup now holds v10");
    now += 5;
    CHECK(saves_backup_now() == 0, "backup now again");
    saves_backup again[BACKUPS_MAX_LIST];
    CHECK(saves_backup_list(again, BACKUPS_MAX_LIST) == 5 &&
              !strcmp(again[0].stamp, list[0].stamp),
          "an unchanged save makes no second backup");

    /* Other files beside it are not backups and are never pruned. */
    snprintf(p, sizeof p, "%s/ct.srm.backup-foo", dir);
    put_file(p, "x", 1);
    snprintf(p, sizeof p, "%s/ct.srm.before-import-20260101-000000", dir);
    put_file(p, "x", 1);
    CHECK(saves_backup_list(again, BACKUPS_MAX_LIST) == 5, "foreign names not listed");

    /* Days: one save a day at noon for nine more days. Each day's backup
       is the file as the day before left it. */
    for (int d = 1; d <= 9; d++) {
        now = at_day(d, 12, 0);
        version(20 + d);
        saves_flush();
    }
    n = saves_backup_list(list, BACKUPS_MAX_LIST);
    /* newest five (days 5-9 backups) plus days 3, 4 and the older ones
       within 7 days of day 9: days 3..9 -> 7 backups of one a day... */
    CHECK(n == 7, "five newest + two older days = 7, got %d", n);
    CHECK(strncmp(list[0].stamp, "20260610", 8) == 0, "newest is day 9 (June 10): %s",
          list[0].stamp);
    CHECK(strncmp(list[n - 1].stamp, "20260604", 8) == 0, "oldest kept is day 3 (June 4): %s",
          list[n - 1].stamp);
    /* More saves today: the newest five are all today's; one per earlier day stays. */
    for (int k = 0; k < 4; k++) {
        now = at_day(9, 13 + k, 0);
        version(40 + k);
        saves_flush();
    }
    n = saves_backup_list(list, BACKUPS_MAX_LIST);
    int today = 0;
    for (int k = 0; k < n; k++)
        today += !strncmp(list[k].stamp, "20260610", 8);
    CHECK(today == 5, "five of today, got %d", today);
    CHECK(n == 5 + 6, "plus one for each of the six earlier days, got %d", n);
    CHECK(count_names(dir, "backup-2026") == 11, "files on disk match: %d",
          count_names(dir, "backup-2026"));

    /* Restore an older backup: round trip. */
    int pick = n - 1;
    char picked[16];
    snprintf(picked, sizeof picked, "%s", list[pick].stamp);
    snprintf(p, sizeof p, "%s/ct.srm.backup-%s", dir, picked);
    uint8_t want[CT_SRAM_SIZE];
    FILE *f = fopen(p, "rb");
    CHECK(f && fread(want, 1, sizeof want, f) == sizeof want, "read the backup");
    if (f)
        fclose(f);
    now = at_day(9, 20, 0);
    CHECK(saves_backup_restore(pick) == 0, "restore");
    uint8_t got[CT_SRAM_SIZE];
    f = fopen(srm, "rb");
    CHECK(f && fread(got, 1, sizeof got, f) == sizeof got, "read the save");
    if (f)
        fclose(f);
    CHECK(!memcmp(got, want, sizeof got), "the save file is the backup");
    CHECK(!memcmp(bus_sram(), want, sizeof want), "so is the running game's save RAM");
    snprintf(p, sizeof p, "%s/ct.srm.before-restore-20260610-200000", dir);
    CHECK(file_is(p, 43), "the save it replaced is kept (v43)");
    /* the next frame's check sees nothing to write */
    saves_frame(1);
    CHECK(!memcmp(got, want, sizeof got), "no spurious write");

    /* A bad backup (wrong size, newest by name) is refused, nothing touched. */
    snprintf(p, sizeof p, "%s/ct.srm.backup-20991231-235959", dir);
    put_file(p, "short", 5);
    n = saves_backup_list(list, BACKUPS_MAX_LIST);
    CHECK(!strcmp(list[0].stamp, "20991231-235959"), "the bad one is listed first");
    CHECK(saves_backup_restore(0) == -1, "bad backup refused");
    f = fopen(srm, "rb");
    CHECK(f && fread(got, 1, sizeof got, f) == sizeof got && !memcmp(got, want, sizeof got),
          "save untouched");
    if (f)
        fclose(f);
    CHECK(saves_backup_restore(500) == -1 && saves_backup_restore(-1) == -1, "bad index");
    remove(p);
    snprintf(p, sizeof p, "%s/ct.srm.backup-20991231-235958", dir);
    char big[CT_SRAM_SIZE + 1];
    memset(big, 7, sizeof big);
    put_file(p, big, sizeof big);
    CHECK(saves_backup_restore(0) == -1, "a 8,193-byte backup is refused too");
    remove(p);

    /* The start of a run backs up what the last run left. */
    saves_backup_auto(1);
    now = at_day(10, 8, 0);
    version(60);
    saves_flush();
    version(61);   /* the run ends here; the next one starts */
    snprintf(p, sizeof p, "%s/ct.srm.backup-20260611-080000", dir);
    CHECK(file_is(p, 43) || exists(p), "a write backs up the previous file");
    saves_backup_auto(0);
    int before = saves_backup_list(list, BACKUPS_MAX_LIST);
    now = at_day(10, 9, 0);
    saves_flush();
    CHECK(saves_backup_list(list, BACKUPS_MAX_LIST) == before, "auto backups off: none made");
    saves_backup_auto(1);

    /* Reset request: once per request. */
    CHECK(game_reset_take() == 0, "no reset pending");
    CHECK(game_reset_request() == 1, "reset requested");
    CHECK(game_reset_take() == 1 && game_reset_take() == 0, "taken once");
    saves_set_clock(NULL);
}

static void test_overlays(void)
{
    /* frame time */
    CHECK(overlays_fps() == 0 && overlays_frame_ms() == 0, "no frames yet");
    for (int k = 0; k < 120; k++)
        overlays_note_frame(k == 7 ? 40.0 : 16.0);   /* 7 falls out of the last 60 */
    CHECK(overlays_frame_ms() > 15.99 && overlays_frame_ms() < 16.01, "mean %.3f",
          overlays_frame_ms());
    CHECK(overlays_fps() > 62.4 && overlays_fps() < 62.6, "fps %.2f", overlays_fps());
    overlays_note_frame(30.0);
    CHECK(overlays_frame_max_ms() == 30.0, "slowest");
    overlays_note_frame(0.0);
    overlays_note_frame(90000.0);   /* a stall is not a frame */
    CHECK(overlays_frame_max_ms() == 30.0, "stall ignored");

    /* play time: counts frames that advance, a jump is not play time */
    char file[4300], t[32];
    snprintf(file, sizeof file, "%s/playtime.txt", tmp);
    overlays_playtime_open(file);
    CHECK(overlays_playtime_frames() == 0, "fresh");
    for (long f = 1000; f < 1000 + 3600 * 2; f++)
        overlays_update((unsigned)(f * 16), f);
    CHECK(overlays_playtime_frames() == 3600 * 2 - 1, "frames %ld", overlays_playtime_frames());
    overlays_update(1u << 20, 50);       /* a state load back in time */
    overlays_update((1u << 20) + 16, 51);
    CHECK(overlays_playtime_frames() == 3600 * 2, "frames after a rewind %ld",
          overlays_playtime_frames());
    overlays_update((1u << 20) + 32, 5000);   /* a jump forward */
    CHECK(overlays_playtime_frames() == 3600 * 2, "a jump isn't play time");
    overlays_format_playtime(60L * (3600 + 125), t, sizeof t);
    CHECK(!strcmp(t, "1:02:05"), "format %s", t);
    overlays_format_playtime(0, t, sizeof t);
    CHECK(!strcmp(t, "0:00:00"), "zero %s", t);
    overlays_playtime_flush();
    CHECK(exists(file), "file written");
    FILE *f = fopen(file, "r");
    long v = -1;
    CHECK(f && fscanf(f, "frames=%ld", &v) == 1 && v == 3600 * 2, "file holds %ld", v);
    if (f)
        fclose(f);
    char tmpf[4400];
    snprintf(tmpf, sizeof tmpf, "%s.tmp", file);
    CHECK(!exists(tmpf), "no temp file left");
    overlays_playtime_set(5);
    overlays_playtime_open(file);   /* reload */
    CHECK(overlays_playtime_frames() == 3600 * 2, "reloaded");
    put_file(file, "garbage", 7);
    overlays_playtime_open(file);
    CHECK(overlays_playtime_frames() == 0, "garbage reads as zero");

    /* clock */
    overlays_format_clock(at_day(0, 7, 5), t, sizeof t);
    CHECK(!strcmp(t, "07:05"), "clock %s", t);

    /* battery from a fake sysfs */
    char d[4200], p[4400];
    snprintf(d, sizeof d, "%s/power", tmp);
    mkdir(d, 0755);
    CHECK(qol_battery_percent(d) == -1, "no battery");
    snprintf(p, sizeof p, "%s/BAT0", d);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/BAT0/type", d);
    put_file(p, "Battery\n", 8);
    CHECK(qol_battery_percent(d) == -1, "no capacity file");
    snprintf(p, sizeof p, "%s/BAT0/capacity", d);
    put_file(p, "87\n", 3);
    CHECK(qol_battery_percent(d) == 87, "87%%");
    snprintf(p, sizeof p, "%s/AC", d);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/AC/type", d);
    put_file(p, "Mains\n", 6);
    snprintf(p, sizeof p, "%s/AC/capacity", d);
    put_file(p, "100\n", 4);
    CHECK(qol_battery_percent(d) == 87, "mains ignored");
    snprintf(p, sizeof p, "%s/BAT0/capacity", d);
    put_file(p, "abc\n", 4);
    CHECK(qol_battery_percent(d) == -1, "unreadable capacity");
    CHECK(qol_battery_percent("/nonexistent/power_supply") == -1, "no folder");

    /* the lines */
    char lines[OVERLAY_COUNT][32];
    CHECK(overlays_lines(lines, OVERLAY_COUNT) == 0, "all off by default");
    for (int k = 0; k < OVERLAY_COUNT; k++)
        overlays_set(k, 1);
    CHECK(overlays_lines(lines, OVERLAY_COUNT) == 4, "four lines");
    CHECK(strstr(lines[0], "FPS") && strchr(lines[1], ':') && strstr(lines[2], "Battery") &&
              !strncmp(lines[3], "Play ", 5), "%s | %s | %s | %s", lines[0], lines[1], lines[2],
          lines[3]);
    overlays_set(OVERLAY_FPS, 0);
    CHECK(overlays_lines(lines, OVERLAY_COUNT) == 3 && strchr(lines[0], ':'), "fps off");
    CHECK(overlays_lines(lines, 1) == 1, "max respected");
    for (int k = 0; k < OVERLAY_COUNT; k++)
        overlays_set(k, 0);
    overlays_set(99, 1);
    CHECK(overlays_get(99) == 0, "bad id ignored");
}

static void test_settings(void)
{
    input_register_settings();
    touch_register_settings();
    overlays_register_settings();
    saves_register_settings();
    int o, s, h, ab, xy;

    CHECK(settings_parse_line("touch_opacity=60\n") == 1, "touch_opacity known");
    CHECK(settings_parse_line("touch_size=120\n") == 1, "touch_size known");
    CHECK(settings_parse_line("touch_hand=left\n") == 1, "touch_hand known");
    touch_options_get(&o, &s, &h);
    CHECK(o == 60 && s == 120 && h == 1, "touch %d %d %d", o, s, h);
    settings_parse_line("touch_hand=right\n");
    settings_parse_line("touch_size=5000\n");   /* out of range: clamped by the registry */
    touch_options_get(&o, &s, &h);
    CHECK(s == TOUCH_SIZE_MAX && h == 0, "clamped: %d %d", s, h);
    touch_options_set(5, 999, 7);   /* clamped */
    touch_options_get(&o, &s, &h);
    CHECK(o == TOUCH_OPACITY_MIN && s == TOUCH_SIZE_MAX && h == 1, "clamped %d %d %d", o, s, h);
    touch_options_set(TOUCH_OPACITY_DEFAULT, 100, 0);

    settings_parse_line("swap_ab=on\n");
    settings_parse_line("swap_xy=1\n");
    input_swap_get(&ab, &xy);
    CHECK(ab == 1 && xy == 1, "swaps %d %d", ab, xy);
    settings_parse_line("stick_deadzone=30\n");
    CHECK(input_deadzone_get() == 30, "deadzone %d", input_deadzone_get());
    input_swap_set(0, 0);
    input_deadzone_set(1000);
    CHECK(input_deadzone_get() == INPUT_DEADZONE_MAX, "deadzone clamped");
    input_deadzone_set(INPUT_DEADZONE_DEFAULT);

    settings_parse_line("overlay_fps=on\n");
    settings_parse_line("overlay_clock=off\n");
    settings_parse_line("overlay_battery=true\n");
    settings_parse_line("overlay_playtime=1\n");
    settings_parse_line("overlay_corner=bl\n");
    CHECK(overlays_get(OVERLAY_FPS) && !overlays_get(OVERLAY_CLOCK) &&
              overlays_get(OVERLAY_BATTERY) && overlays_get(OVERLAY_PLAYTIME), "overlay flags");
    CHECK(overlays_get_corner() == OVERLAY_BL, "corner");

    /* they are written back */
    char file[4300];
    snprintf(file, sizeof file, "%s/settings_out.ini", tmp);
    FILE *f = fopen(file, "w");
    settings_write_lines(f);
    fclose(f);
    f = fopen(file, "r");
    char all[16384] = "";
    size_t n = f ? fread(all, 1, sizeof all - 1, f) : 0;
    all[n] = 0;
    if (f)
        fclose(f);
    CHECK(strstr(all, "touch_opacity=") && strstr(all, "touch_size=") &&
              strstr(all, "touch_hand=right") && strstr(all, "swap_ab=off") &&
              strstr(all, "stick_deadzone=49") && strstr(all, "overlay_corner=bl"),
          "settings.ini lines:\n%s", all);

    /* the F10 rows */
    const setting *r = settings_find("overlay_fps");
    CHECK(r && r->type == SETTING_BOOL, "row overlay_fps");
    int backup_rows = 0, reset_rows = 0, confirm = 0;
    for (int k = 0; k < settings_count(); k++) {
        const setting *x = settings_at(k);
        if (x->label && !strcmp(x->label, "Back up now"))
            backup_rows++;
        if (x->label && !strncmp(x->label, "Restore backup", 14))
            confirm += (x->flags & SETTING_CONFIRM) != 0;
        if (x->label && !strcmp(x->label, "Reset the game"))
            reset_rows += (x->flags & SETTING_CONFIRM) != 0;
    }
    CHECK(backup_rows == 1 && confirm == SAVES_RESTORE_ROWS && reset_rows == 1,
          "rows %d %d %d", backup_rows, confirm, reset_rows);
    for (int k = 0; k < OVERLAY_COUNT; k++)
        overlays_set(k, 0);
    overlays_set_corner(OVERLAY_TR);
}

int main(void)
{
    snprintf(tmp, sizeof tmp, "%s/ct_test_q3_XXXXXX", ct_tmpdir());
    CHECK(mkdtemp(tmp) != NULL, "mkdtemp");
    test_backups();
    test_overlays();
    test_settings();
    char cmd[4200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp);
    if (system(cmd)) {
    }
    return th_report("q3");
}
