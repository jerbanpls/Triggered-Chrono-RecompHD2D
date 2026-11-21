/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Quality of life (qol.h). */
#include "qol.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "actions.h"
#include "bindings.h"
#include "hd2d/hd2d.h"
#include "input.h"
#include "osd_text.h"
#include "png.h"
#include "saves.h"
#include "savestate.h"
#include "sched.h"
#include "settings.h"   /* port: the menu's rows (qol_register_settings) */
#include "pngio.h"      /* port: the save state thumbnails, for the menu */
#include "platform.h"   /* port: ct_dlopen, ct_home; Windows renames (rename, mkdir...) */

/* ---- settings (settings.ini; qol_setting) ---- */

enum { AUTO, ON, OFF };
enum { BATT_OFF, BATT_LIGHT, BATT_SAVER };

typedef struct {
    int ffwd_speed;       /* 2-4 */
    uint16_t turbo;       /* buttons with autofire */
    int pause_focus;      /* AUTO: on, except in Game Mode */
    int pause_pad;        /* ON / OFF */
    int resume;           /* ON / OFF: auto state on quit, prompt at start */
    int battery;          /* BATT_* */
    int crt;              /* plain picture */
    int integer;          /* plain picture: whole multiples */
    int always_run;
} qol_settings;
static qol_settings set = {3, 0, AUTO, ON, ON, BATT_OFF, 0, 1, 0};
/* --qol-set changes set for this run only: settings.ini gets saved, the
   settings as they were before, plus what is toggled during the run. */
static qol_settings saved;
static int overridden;

static const struct { const char *name; uint16_t bit; } button_names[] = {
    {"a", PAD_A}, {"b", PAD_B}, {"x", PAD_X}, {"y", PAD_Y}, {"l", PAD_L}, {"r", PAD_R},
    {"start", PAD_START}, {"select", PAD_SELECT},
};

static int on_off(const char *v)
{
    return !strcmp(v, "on") || !strcmp(v, "1") || !strcmp(v, "yes") || !strcmp(v, "true");
}

int qol_setting(const char *key, const char *v)
{
    if (!strcmp(key, "ffwd_speed")) {
        int n = atoi(v);
        set.ffwd_speed = n < 2 ? 2 : n > 4 ? 4 : n;
    } else if (!strcmp(key, "turbo")) {
        set.turbo = 0;
        char buf[64];
        snprintf(buf, sizeof buf, "%s", v);
        for (char *t = strtok(buf, ","); t; t = strtok(NULL, ","))
            for (size_t k = 0; k < sizeof button_names / sizeof *button_names; k++)
                if (!strcmp(t, button_names[k].name))
                    set.turbo |= button_names[k].bit;
    } else if (!strcmp(key, "pause_on_focus_loss")) {
        set.pause_focus = !strcmp(v, "auto") ? AUTO : on_off(v) ? ON : OFF;
    } else if (!strcmp(key, "pause_on_disconnect")) {
        set.pause_pad = on_off(v) ? ON : OFF;
    } else if (!strcmp(key, "resume")) {
        set.resume = on_off(v) ? ON : OFF;
    } else if (!strcmp(key, "battery_mode")) {
        set.battery = !strcmp(v, "light") ? BATT_LIGHT : !strcmp(v, "saver") ? BATT_SAVER : BATT_OFF;
    } else if (!strcmp(key, "crt")) {
        set.crt = on_off(v);
    } else if (!strcmp(key, "integer_scale")) {
        set.integer = on_off(v);
    } else if (!strcmp(key, "always_run")) {
        set.always_run = on_off(v);
        sched_set_pref(SCHED_PREF_ALWAYS_RUN, set.always_run);
    } else {
        return 0;
    }
    return 1;
}

void qol_settings_write(FILE *f)
{
    static const char *const tri[] = {"auto", "on", "off"};
    static const char *const batt[] = {"off", "light", "saver"};
    const qol_settings *v = overridden ? &saved : &set;
    fprintf(f, "ffwd_speed=%d\nturbo=", v->ffwd_speed);
    const char *sep = "";
    for (size_t k = 0; k < sizeof button_names / sizeof *button_names; k++)
        if (v->turbo & button_names[k].bit) {
            fprintf(f, "%s%s", sep, button_names[k].name);
            sep = ",";
        }
    fprintf(f, "\npause_on_focus_loss=%s\npause_on_disconnect=%s\nresume=%s\n"
               "battery_mode=%s\ncrt=%s\ninteger_scale=%s\nalways_run=%s\n",
            tri[v->pause_focus], tri[v->pause_pad], tri[v->resume], batt[v->battery],
            v->crt ? "on" : "off", v->integer ? "on" : "off", v->always_run ? "on" : "off");
}

/* ---- state ---- */

static qol_host host;
static int inited, hd2d, game_mode, states_on, recording, scripted;
static char states_dir[4096];
static const char *states_dir_opt, *load_opt;
static int no_resume_opt;

enum {   /* why the game is paused (bits) */
    P_USER = 1, P_FOCUS = 2, P_PAD = 4, P_PROMPT = 8,
};
static int paused, pause_shown;
static long idle_ticks;
static uint16_t swallow[4];          /* held through an unpause: kept from the game */
static uint16_t last_live[4];

static int ffwd_hold, ffwd_toggle, muted;
static int slot = 1;                 /* the selected slot (1-4) */
static int want_save = -1, want_load = -1, want_shot;
static long want_since;              /* frame a save was asked at */
static long frames_run;              /* this session (since start or the last load) */
static int user_preset = -1;         /* HD-2D preset before the battery changed it */
static int on_batt, batt_checked;
static Uint32 batt_at;
static int focus_pause;              /* pause_on_focus_loss, auto decided */
static int focus_lost;               /* paused for it */
static int focus_pending;            /* lost: pause if it stays lost a moment */
static Uint32 focus_since;
static SDL_Window *focus_win;

/* scripted actions (--qol-action F:NAME) */
#define MAX_ACTIONS 32
static struct { long frame; char name[24]; } actions[MAX_ACTIONS];
static int n_actions;

/* ---- on-screen messages ----
   As main.c's: HD-2D's or the plain picture's (osd_text.h), [token]s as
   the buttons of the device in use; on stderr as their labels. */

void qol_osd(const char *msg)
{
    char plain[256];
    osd_text_plain(msg, plain, sizeof plain);
    fprintf(stderr, "ct_sdl: %s\n", plain);
    if (hd2d)
        hd2d_osd(msg);
    else
        osd_sdl_show(msg);
}

/* ---- files ---- */

static int mkdirs(const char *dir)
{
    char p[4096];
    snprintf(p, sizeof p, "%s", dir);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') {
            *s = 0;
            if (mkdir(p, 0755) && errno != EEXIST)
                return -1;
            *s = '/';
        }
    return mkdir(p, 0755) && errno != EEXIST ? -1 : 0;
}

static const char *slot_name(int s)
{
    static const char *const names[] = {"quick", "slot1", "slot2", "slot3", "slot4", "auto"};
    return names[s];
}
#define SLOT_AUTO 5

static void slot_path(char *out, size_t n, int s, const char *ext)
{
    snprintf(out, n, "%s/%s.%s", states_dir, slot_name(s), ext);
}

static void slot_label(char *out, size_t n, int s)
{
    if (s == 0)
        snprintf(out, n, "Quick slot");
    else if (s == SLOT_AUTO)
        snprintf(out, n, "Resume point");
    else
        snprintf(out, n, "Slot %d", s);
}

void qol_pictures_dir(char *out, size_t n)
{
    const char *home = ct_home();
    const char *v = getenv("XDG_PICTURES_DIR");
    char dir[4096] = "";
    if (v && *v) {
        snprintf(dir, sizeof dir, "%s", v);
    } else {
        /* user-dirs.dirs: XDG_PICTURES_DIR="$HOME/Pictures" */
        char conf[4096], line[4096];
        const char *xc = getenv("XDG_CONFIG_HOME");
        if (xc && *xc)
            snprintf(conf, sizeof conf, "%s/user-dirs.dirs", xc);
        else
            snprintf(conf, sizeof conf, "%s/.config/user-dirs.dirs", home ? home : "");
        FILE *f = fopen(conf, "r");
        while (f && fgets(line, sizeof line, f)) {
            char *q = strstr(line, "XDG_PICTURES_DIR=\"");
            if (line[0] == '#' || !q || q != line)
                continue;
            q += strlen("XDG_PICTURES_DIR=\"");
            char *end = strchr(q, '"');
            if (!end)
                continue;
            *end = 0;
            if (!strncmp(q, "$HOME", 5))
                snprintf(dir, sizeof dir, "%s%s", home ? home : "", q + 5);
            else
                snprintf(dir, sizeof dir, "%s", q);
        }
        if (f)
            fclose(f);
    }
    if (!dir[0])
        snprintf(dir, sizeof dir, "%s/Pictures", home ? home : ".");
    snprintf(out, n, "%s/ct-recomp", dir);
}

/* ---- power (battery_mode) ---- */

static int read_line(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int ok = fgets(buf, (int)n, f) != NULL;
    fclose(f);
    if (!ok)
        return -1;
    buf[strcspn(buf, "\r\n")] = 0;
    return 0;
}

int qol_on_battery(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    int ac = 0, discharging = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char p[4096], type[64], v[64];
        snprintf(p, sizeof p, "%s/%s/type", dir, e->d_name);
        if (read_line(p, type, sizeof type))
            continue;
        if (!strcmp(type, "Battery")) {
            snprintf(p, sizeof p, "%s/%s/status", dir, e->d_name);
            if (!read_line(p, v, sizeof v) && !strcmp(v, "Discharging"))
                discharging = 1;
        } else {   /* Mains, USB, USB_PD...: online=1 is outside power */
            snprintf(p, sizeof p, "%s/%s/online", dir, e->d_name);
            if (!read_line(p, v, sizeof v) && !strcmp(v, "1"))
                ac = 1;
        }
    }
    closedir(d);
    return discharging && !ac;
}

int qol_battery_percent(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d)
        return -1;
    int best = -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;
        char p[4096], v[64];
        snprintf(p, sizeof p, "%s/%s/type", dir, e->d_name);
        if (read_line(p, v, sizeof v) || strcmp(v, "Battery"))
            continue;
        snprintf(p, sizeof p, "%s/%s/capacity", dir, e->d_name);
        char *end;
        if (read_line(p, v, sizeof v))
            continue;
        long c = strtol(v, &end, 10);
        if (end != v && c >= 0 && c <= 100 && (best < 0 || c < best))
            best = (int)c;   /* several (a Legion Go's grips): the lowest */
    }
    closedir(d);
    return best;
}

static void battery_check(void)
{
    if (set.battery == BATT_OFF && !on_batt)
        return;
    Uint32 now = SDL_GetTicks();
    if (batt_checked && now - batt_at < 5000)
        return;
    batt_checked = 1;
    batt_at = now;
    const char *dir = getenv("CT_POWER_SUPPLY_DIR");   /* tests */
    int b = set.battery != BATT_OFF && qol_on_battery(dir ? dir : "/sys/class/power_supply");
    if (b == on_batt)
        return;
    on_batt = b;
    if (hd2d) {
        if (b && hd2d_preset() != HD2D_OFF && hd2d_preset() != HD2D_DECK) {
            user_preset = hd2d_preset();
            hd2d_set_preset(HD2D_DECK);
        } else if (!b && user_preset >= 0) {
            hd2d_set_preset(user_preset);
            user_preset = -1;
        }
    }
    qol_osd(b ? (set.battery == BATT_SAVER ? "On battery: lighter look, 30 Hz picture"
                                           : "On battery: lighter look")
              : "On AC power: full look");
}

int qol_user_preset(int current)
{
    return user_preset >= 0 ? user_preset : current;
}

/* ---- suspend (logind PrepareForSleep over D-Bus, libdbus loaded at run
   time if it is there; otherwise only the wake-up is noticed) ---- */

typedef struct DBusConnection DBusConnection;
typedef struct DBusMessage DBusMessage;
typedef struct {
    const char *name, *message;
    unsigned int dummy;
    void *padding1, *spare[4];   /* room to spare over libdbus's DBusError */
} DBusErr;
#define DBUS_BUS_SYSTEM 1
#define DBUS_T_INVALID 0
#define DBUS_T_BOOLEAN 'b'
#define DBUS_T_STRING 's'
#define DBUS_T_UNIX_FD 'h'

static struct {
    void (*error_init)(DBusErr *);
    void (*error_free)(DBusErr *);
    DBusConnection *(*bus_get_private)(int, DBusErr *);
    void (*set_exit_on_disconnect)(DBusConnection *, uint32_t);
    void (*add_match)(DBusConnection *, const char *, DBusErr *);
    uint32_t (*read_write)(DBusConnection *, int);
    DBusMessage *(*pop)(DBusConnection *);
    uint32_t (*is_signal)(DBusMessage *, const char *, const char *);
    uint32_t (*get_args)(DBusMessage *, DBusErr *, int, ...);
    void (*unref)(DBusMessage *);
    DBusMessage *(*new_call)(const char *, const char *, const char *, const char *);
    uint32_t (*append_args)(DBusMessage *, int, ...);
    DBusMessage *(*call)(DBusConnection *, DBusMessage *, int, DBusErr *);
    DBusConnection *conn;
    int fd;   /* logind delay lock, or -1 */
} dbus = {.fd = -1};

static void sleep_inhibit(void)
{
    if (!dbus.conn || dbus.fd >= 0)
        return;
    DBusMessage *m = dbus.new_call("org.freedesktop.login1", "/org/freedesktop/login1",
                                   "org.freedesktop.login1.Manager", "Inhibit");
    const char *what = "sleep", *who = "ct-recomp", *why = "Save the game before sleeping",
               *mode = "delay";
    if (!m || !dbus.append_args(m, DBUS_T_STRING, &what, DBUS_T_STRING, &who, DBUS_T_STRING, &why,
                                DBUS_T_STRING, &mode, DBUS_T_INVALID)) {
        if (m)
            dbus.unref(m);
        return;
    }
    DBusErr err;
    dbus.error_init(&err);
    DBusMessage *r = dbus.call(dbus.conn, m, 1000, &err);
    dbus.unref(m);
    int fd = -1;
    if (r && dbus.get_args(r, &err, DBUS_T_UNIX_FD, &fd, DBUS_T_INVALID))
        dbus.fd = fd;
    if (r)
        dbus.unref(r);
    if (err.name) {
        fprintf(stderr, "ct_sdl: no sleep delay lock (%s): the resume point is saved as "
                        "sleep starts\n", err.message ? err.message : err.name);
        dbus.error_free(&err);
    }
}

static void sleep_watch_init(void)
{
    void *lib = ct_dlopen("libdbus-1.so.3");
    if (!lib) {
        fprintf(stderr, "ct_sdl: no libdbus: suspend not watched (the resume point is saved "
                        "on quit)\n");
        return;
    }
#define SYM(field, name) *(void **)&dbus.field = ct_dlsym(lib, name)
    SYM(error_init, "dbus_error_init");
    SYM(error_free, "dbus_error_free");
    SYM(bus_get_private, "dbus_bus_get_private");
    SYM(set_exit_on_disconnect, "dbus_connection_set_exit_on_disconnect");
    SYM(add_match, "dbus_bus_add_match");
    SYM(read_write, "dbus_connection_read_write");
    SYM(pop, "dbus_connection_pop_message");
    SYM(is_signal, "dbus_message_is_signal");
    SYM(get_args, "dbus_message_get_args");
    SYM(unref, "dbus_message_unref");
    SYM(new_call, "dbus_message_new_method_call");
    SYM(append_args, "dbus_message_append_args");
    SYM(call, "dbus_connection_send_with_reply_and_block");
#undef SYM
    if (!dbus.error_init || !dbus.error_free || !dbus.bus_get_private ||
        !dbus.set_exit_on_disconnect || !dbus.add_match || !dbus.read_write || !dbus.pop ||
        !dbus.is_signal || !dbus.get_args || !dbus.unref || !dbus.new_call ||
        !dbus.append_args || !dbus.call)
        return;
    DBusErr err;
    dbus.error_init(&err);
    dbus.conn = dbus.bus_get_private(DBUS_BUS_SYSTEM, &err);
    if (dbus.conn) {
        dbus.set_exit_on_disconnect(dbus.conn, 0);
        dbus.add_match(dbus.conn, "type='signal',interface='org.freedesktop.login1.Manager',"
                                  "member='PrepareForSleep'", &err);
    }
    if (err.name) {
        fprintf(stderr, "ct_sdl: suspend not watched (%s)\n",
                err.message ? err.message : err.name);
        dbus.error_free(&err);
        dbus.conn = NULL;   /* (a private connection, left open) */
        return;
    }
    sleep_inhibit();
}

static int want_auto;   /* the resume point, at the next frame edge that can */
static int auto_wanted(void);

static void sleep_release(void)
{
    if (dbus.fd >= 0) {
        close(dbus.fd);   /* let it sleep */
        dbus.fd = -1;
    }
}

/* The system is about to sleep (or the app goes to the background), or
   has woken up. Called inside a frame too: the save waits for its end. */
static void sleeping(int going)
{
    if (going) {
        fprintf(stderr, "ct_sdl: going to sleep\n");
        if (host.audio_restart)
            host.audio_restart();   /* silence now, not a buzz while asleep */
        if (auto_wanted())
            want_auto = 1;   /* then the delay lock goes (pending) */
        else
            sleep_release();
    } else {
        fprintf(stderr, "ct_sdl: woke up\n");
        if (host.audio_restart)
            host.audio_restart();
        if (host.pacing_reset)
            host.pacing_reset();
        sleep_inhibit();
    }
}

static void sleep_poll(void)
{
    if (!dbus.conn)
        return;
    dbus.read_write(dbus.conn, 0);
    DBusMessage *m;
    while ((m = dbus.pop(dbus.conn))) {
        uint32_t going = 0;
        DBusErr err;
        dbus.error_init(&err);
        if (dbus.is_signal(m, "org.freedesktop.login1.Manager", "PrepareForSleep") &&
            dbus.get_args(m, &err, DBUS_T_BOOLEAN, &going, DBUS_T_INVALID))
            sleeping(going != 0);
        if (err.name)
            dbus.error_free(&err);
        dbus.unref(m);
    }
}

/* Woken from a suspend nobody told us about: CLOCK_BOOTTIME runs through
   suspend, CLOCK_MONOTONIC doesn't. */
static void gap_check(void)
{
#ifdef CLOCK_BOOTTIME
    static double last = -1;
    struct timespec b, m;
    if (clock_gettime(CLOCK_BOOTTIME, &b) || clock_gettime(CLOCK_MONOTONIC, &m))
        return;
    double d = (double)(b.tv_sec - m.tv_sec) + (double)(b.tv_nsec - m.tv_nsec) * 1e-9;
    if (last >= 0 && d - last > 1.0) {
        fprintf(stderr, "ct_sdl: the system slept %.1f s\n", d - last);
        if (host.audio_restart)
            host.audio_restart();
        if (host.pacing_reset)
            host.pacing_reset();
    }
    last = d;
#endif
}

/* ---- pause ---- */

static void pause_set(int why)
{
    static const char *const name[] = {"paused", "the window lost focus", "a controller went",
                                       "", "a resume point is there"};
    fprintf(stderr, "ct_sdl: paused (%s)\n", name[why & P_USER ? 0 : why & P_FOCUS ? 1
                                                 : why & P_PAD ? 2 : 4]);
    if (!paused && host.audio_restart)
        host.audio_restart();
    paused |= why;
    pause_shown = 0;
    idle_ticks = 0;
}

static void pause_clear(int why)
{
    int was = paused;
    paused &= ~why;
    if (was && !paused) {
        for (int p = 0; p < 4; p++)
            swallow[p] = 0xFFFF;   /* whatever is held now (the next frame's buttons)
                                      is not a press for the game until let go */
        if (host.pacing_reset)
            host.pacing_reset();
        fprintf(stderr, "ct_sdl: playing\n");
    }
}

int qol_paused(void) { return paused != 0; }

void qol_pause_toggle(void)
{
    if (paused & ~P_PROMPT)
        pause_clear(paused & ~P_PROMPT);
    else
        pause_set(P_USER);
}

static const char *pause_text(void)
{
    if (paused & P_PROMPT)
        return "Resume? [a] yes [b] no";
    if (paused & P_PAD)
        return "Paused: a controller went - [a] goes on";
    return "Paused";
}

/* ---- save states ---- */

static int states_ready(void)
{
    if (!states_on)
        qol_osd("Save states are off (--no-save)");
    return states_on;
}

void qol_save_slot(int s)
{
    if (!states_ready())
        return;
    want_save = s;
    want_since = sched_frame_count();
}

void qol_load_slot(int s)
{
    if (!states_ready())
        return;
    if (recording) {
        qol_osd("Not while recording (--record)");
        return;
    }
    want_load = s;
}

void qol_quicksave(void) { qol_save_slot(0); }
void qol_quickload(void) { qol_load_slot(0); }

void qol_slot_next(void)
{
    slot = slot % 4 + 1;
    char msg[48], p[4200];
    slot_path(p, sizeof p, slot, "state");
    snprintf(msg, sizeof msg, "Slot %d%s", slot, access(p, F_OK) ? " (empty)" : "");
    qol_osd(msg);
}

/* Try now; 0 done (or failed for good), 1 not possible at this edge. */
static int do_save(int s, int quiet)
{
    char path[4200], label[32], msg[64];
    if (savestate_ready(NULL))
        return 1;
    slot_label(label, sizeof label, s);
    if (mkdirs(states_dir)) {
        snprintf(msg, sizeof msg, "Cannot save: no folder for states");
        qol_osd(msg);
        return 0;
    }
    slot_path(path, sizeof path, s, "state");
    const char *why = NULL;
    if (savestate_write_file(path, &why)) {
        snprintf(msg, sizeof msg, "%s: cannot save", label);
        qol_osd(msg);
        fprintf(stderr, "ct_sdl: %s: %s\n", path, why ? why : "?");
        return 0;
    }
    /* the thumbnail: the frame as it was, atomically */
    char png[4200], tmp[4300];
    slot_path(png, sizeof png, s, "png");
    snprintf(tmp, sizeof tmp, "%s.tmp", png);
    if (!png_write_bgrx(tmp, sched_frame(), SCHED_WIDTH, SCHED_HEIGHT))
        rename(tmp, png);
    else
        remove(tmp);
    fprintf(stderr, "ct_sdl: state saved to %s (frame %ld)\n", path, sched_frame_count());
    if (!quiet) {
        snprintf(msg, sizeof msg, "%s: saved", label);
        qol_osd(msg);
    }
    return 0;
}

static int do_load(int s)
{
    char path[4200], label[32], msg[64];
    slot_label(label, sizeof label, s);
    slot_path(path, sizeof path, s, "state");
    if (access(path, F_OK)) {
        snprintf(msg, sizeof msg, "%s: empty", label);
        qol_osd(msg);
        return -1;
    }
    const char *why = NULL;
    if (savestate_read_file(path, &why)) {
        snprintf(msg, sizeof msg, "%s: cannot load (see the log)", label);
        qol_osd(msg);
        fprintf(stderr, "ct_sdl: %s: %s\n", path, why ? why : "?");
        return -1;
    }
    saves_after_state_load();   /* the cartridge's own save RAM stays */
    if (host.audio_restart)
        host.audio_restart();
    if (host.pacing_reset)
        host.pacing_reset();
    frames_run = 0;
    fprintf(stderr, "ct_sdl: state loaded from %s (frame %ld)\n", path, sched_frame_count());
    snprintf(msg, sizeof msg, "%s: loaded", label);
    qol_osd(msg);
    return 0;
}

/* A resume point is worth saving: on, and something was played since the
   start or the last load (not a quit from the prompt). */
static int auto_wanted(void)
{
    return states_on && set.resume == ON && frames_run >= 60 && !(paused & P_PROMPT);
}

/* The resume point at a quit: now if possible, else at the next frame
   edges that allow it (a few more frames run, unseen). Between frames. */
static void save_auto(const char *why)
{
    if (!auto_wanted()) {
        if (states_on && set.resume == ON)
            fprintf(stderr, "ct_sdl: resume point not saved (%s): nothing played\n", why);
        return;
    }
    int k = 0;
    while (k++ < 600 && do_save(SLOT_AUTO, 1))
        sched_run_frame();
    fprintf(stderr, "ct_sdl: resume point %s (%s)\n", k <= 600 ? "saved" : "NOT saved", why);
}

void qol_quit(int quit)
{
    if (quit)
        save_auto("quit");
}

/* ---- screenshots ---- */

void qol_screenshot(void) { want_shot = 1; }

static void do_shot(void)
{
    char dir[4096], path[4300], msg[64];
    qol_pictures_dir(dir, sizeof dir);
    if (mkdirs(dir)) {
        qol_osd("Cannot save a screenshot: no Pictures folder");
        return;
    }
    time_t t = time(NULL);
    struct tm tm;
    localtime_r(&t, &tm);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &tm);
    snprintf(path, sizeof path, "%s/ct-recomp-%s.png", dir, stamp);
    for (int n = 2; !access(path, F_OK) && n < 100; n++)
        snprintf(path, sizeof path, "%s/ct-recomp-%s-%d.png", dir, stamp, n);
    if (!host.shot || host.shot(path)) {
        qol_osd("Cannot save a screenshot");
        return;
    }
    fprintf(stderr, "ct_sdl: screenshot %s\n", path);
    snprintf(msg, sizeof msg, "Screenshot: %s", strrchr(path, '/') + 1);
    qol_osd(msg);
}

/* ---- fast-forward, turbo, toggles ---- */

static void ffwd_changed(int was)
{
    int now = ffwd_hold || ffwd_toggle;
    if (now == was)
        return;
    muted = now;
    if (host.audio_restart)
        host.audio_restart();   /* no stale sound either way */
    if (!now && host.pacing_reset)
        host.pacing_reset();
    char msg[32];
    snprintf(msg, sizeof msg, now ? "Fast-forward x%d" : "Normal speed", set.ffwd_speed);
    qol_osd(msg);
}

void qol_ffwd_set(int on)
{
    int was = ffwd_hold || ffwd_toggle;
    ffwd_hold = on != 0;
    ffwd_changed(was);
}

void qol_ffwd_toggle(void)
{
    int was = ffwd_hold || ffwd_toggle;
    ffwd_toggle = !ffwd_toggle;
    ffwd_changed(was);
}

int qol_speed(void) { return ffwd_hold || ffwd_toggle ? set.ffwd_speed : 1; }
int qol_audio_muted(void) { return muted; }

void qol_turbo_toggle(uint16_t buttons)
{
    buttons &= PAD_A | PAD_B | PAD_X | PAD_Y | PAD_L | PAD_R;
    if (!buttons)
        set.turbo = 0;
    else
        set.turbo ^= buttons;
    saved.turbo = set.turbo;
    char msg[64] = "Turbo: off";
    if (set.turbo) {
        size_t n = (size_t)snprintf(msg, sizeof msg, "Turbo:");
        for (size_t k = 0; k < 6; k++)
            if (set.turbo & button_names[k].bit)
                n += (size_t)snprintf(msg + n, sizeof msg - n, " %c", button_names[k].name[0] - 32);
    }
    qol_osd(msg);
    if (host.save_settings)
        host.save_settings();
}

uint16_t qol_buttons(int port, uint16_t live, long frame)
{
    if (port < 0 || port > 3)
        return live;
    last_live[port] = live;
    swallow[port] &= live;   /* until released */
    live &= (uint16_t)~swallow[port];
    if (set.turbo && !qol_turbo_phase(frame))
        live &= (uint16_t)~set.turbo;
    return live;
}

void qol_always_run_toggle(void)
{
    set.always_run = !set.always_run;
    saved.always_run = set.always_run;
    sched_set_pref(SCHED_PREF_ALWAYS_RUN, set.always_run);
    qol_osd(set.always_run ? "Always run: on (hold the run button to walk)" : "Always run: off");
    if (host.save_settings)
        host.save_settings();
}

void qol_crt_toggle(void)
{
    set.crt = !set.crt;
    saved.crt = set.crt;
    qol_osd(set.crt ? (hd2d ? "CRT look: on (original picture only)" : "CRT look: on")
                    : "CRT look: off");
    if (host.save_settings)
        host.save_settings();
}

/* ---- options ---- */

int qol_option(int argc, char **argv, int *k)
{
    const char *a = argv[*k];
    if (!strcmp(a, "--states-dir") && *k + 1 < argc) {
        states_dir_opt = argv[++*k];
    } else if (!strcmp(a, "--load-state") && *k + 1 < argc) {
        load_opt = argv[++*k];
    } else if (!strcmp(a, "--no-resume")) {
        no_resume_opt = 1;
    } else if (!strcmp(a, "--qol-set") && *k + 1 < argc) {   /* a settings.ini line, for now */
        char key[32], val[64];
        if (!overridden) {
            saved = set;
            overridden = 1;
        }
        if (sscanf(argv[++*k], "%31[^=]=%63s", key, val) != 2 || !qol_setting(key, val))
            return -1;
    } else if (!strcmp(a, "--qol-action") && *k + 1 < argc) {
        char *end;
        long f = strtol(argv[++*k], &end, 10);
        if (*end != ':' || !end[1] || n_actions == MAX_ACTIONS)
            return -1;
        actions[n_actions].frame = f;
        snprintf(actions[n_actions].name, sizeof actions[n_actions].name, "%s", end + 1);
        n_actions++;
    } else {
        return 0;
    }
    return 1;
}

void qol_usage(FILE *out)
{
    fputs("Quality of life (docs/USER-GUIDE.md):\n"
          "  F6 / F9 (L4 / R4)   quick save / quick load; with Shift: the selected slot\n"
          "  F7                  select slot 1-4      F8, Print Screen  screenshot\n"
          "  Tab held (L5), `    fast-forward (held / on-off)\n"
          "  Ctrl+T              turbo for the buttons held   Ctrl+R  CRT look (original picture)\n"
          "  Caps Lock           always run on / off          Pause, P  pause\n"
          "  --states-dir DIR    save states there (default: the save folder's states/)\n"
          "  --load-state FILE   start from a save state      --no-resume  no resume question\n"
          "  --qol-set KEY=VALUE a quality-of-life setting for this run (settings.ini names)\n"
          "  --qol-action F:NAME testing: an action after frame F (qol.c run_action)\n"
          "  --import-srm FILE / --export-srm FILE  an 8 KiB save RAM file in / out, then stop\n",
          out);
}

/* ---- init ---- */

static void on_action(ct_action a, int down, void *user);

void qol_init(const qol_host *h, const char *game_id, int gm, int hd, int states, int rec,
              int scr)
{
    host = *h;
    game_mode = gm;
    hd2d = hd;
    recording = rec;
    scripted = scr;
    states_on = states || states_dir_opt;
    inited = 1;
    /* auto: not in Game Mode, where gamescope's focus comes and goes */
    focus_pause = set.pause_focus == AUTO ? !game_mode : set.pause_focus == ON;
    sched_set_pref(SCHED_PREF_ALWAYS_RUN, set.always_run);
    actions_set_handler(ACTION_QUICKSAVE, on_action, NULL);
    actions_set_handler(ACTION_QUICKLOAD, on_action, NULL);
    actions_set_handler(ACTION_FFWD, on_action, NULL);
    actions_set_handler(ACTION_SCREENSHOT, on_action, NULL);
    if (states_dir_opt) {
        snprintf(states_dir, sizeof states_dir, "%s", states_dir_opt);
    } else {
        char *dir = SDL_GetPrefPath("ct-recomp", game_id);
        if (dir) {
            snprintf(states_dir, sizeof states_dir, "%sstates", dir);
            SDL_free(dir);
        } else {
            states_on = 0;
        }
    }
    if (states_on)
        fprintf(stderr, "ct_sdl: save states in %s\n", states_dir);
    if (load_opt) {
        const char *why = NULL;
        if (savestate_read_file(load_opt, &why))
            fprintf(stderr, "ct_sdl: --load-state %s: %s\n", load_opt, why ? why : "?");
        else
            fprintf(stderr, "ct_sdl: started from the state %s (frame %ld)\n", load_opt,
                    sched_frame_count());
    } else if (states_on && set.resume == ON && !no_resume_opt && !scripted && !recording) {
        char p[4200];
        slot_path(p, sizeof p, SLOT_AUTO, "state");
        if (!access(p, F_OK))
            pause_set(P_PROMPT);   /* "Resume?" until answered */
    }
    if (states_on && set.resume == ON)
        sleep_watch_init();
}

/* ---- events ---- */

static void prompt_answer(int yes)
{
    char p[4200], old[4200];
    if (yes) {
        want_load = SLOT_AUTO;   /* between frames (pending) */
    } else {
        slot_path(p, sizeof p, SLOT_AUTO, "state");
        snprintf(old, sizeof old, "%s/auto-previous.state", states_dir);
        rename(p, old);   /* kept once, in case of a slip */
        slot_path(p, sizeof p, SLOT_AUTO, "png");
        snprintf(old, sizeof old, "%s/auto-previous.png", states_dir);
        rename(p, old);
        qol_osd("Starting over");
    }
    pause_clear(P_PROMPT);
}

/* The keys of what has no action (yet): a bound key (bindings.h:
   settings.ini key_*) always goes to its action instead. Shift with the
   quick save / load key saves to / loads the selected slot. */
static int key_action(SDL_Scancode sc, SDL_Keymod mod)
{
    ct_action bound = bindings_for_key(bindings_current(), sc);
    if (mod & KMOD_SHIFT) {
        if (bound == ACTION_QUICKSAVE) {
            qol_save_slot(slot);
            return 1;
        }
        if (bound == ACTION_QUICKLOAD) {
            qol_load_slot(slot);
            return 1;
        }
    }
    if (bound != ACTION_NONE)
        return 0;
    int ctrl = (mod & KMOD_CTRL) != 0;
    switch (sc) {
    case SDL_SCANCODE_F7: qol_slot_next(); return 1;
    case SDL_SCANCODE_GRAVE: qol_ffwd_toggle(); return 1;
    case SDL_SCANCODE_PRINTSCREEN: qol_screenshot(); return 1;
    case SDL_SCANCODE_CAPSLOCK: qol_always_run_toggle(); return 1;
    case SDL_SCANCODE_PAUSE:
    case SDL_SCANCODE_P: qol_pause_toggle(); return 1;
    case SDL_SCANCODE_T:
        if (!ctrl)
            return 0;
        qol_turbo_toggle(last_live[0]);
        return 1;
    case SDL_SCANCODE_R:
        if (!ctrl)
            return 0;
        qol_crt_toggle();
        return 1;
    default: return 0;
    }
}

/* The frontend actions (actions.h) this implements. */
static void on_action(ct_action a, int down, void *user)
{
    (void)user;
    switch (a) {
    case ACTION_QUICKSAVE: if (down) qol_quicksave(); break;
    case ACTION_QUICKLOAD: if (down) qol_quickload(); break;
    case ACTION_FFWD: qol_ffwd_set(down); break;
    case ACTION_SCREENSHOT: if (down) qol_screenshot(); break;
    default: break;
    }
}

int qol_event(const SDL_Event *ev, long frame)
{
    (void)frame;
    if (!inited)
        return 0;
    switch (ev->type) {
    case SDL_KEYDOWN:
        if (ev->key.repeat)
            return (paused & P_PROMPT) != 0;
        if (paused & P_PROMPT) {
            SDL_Scancode sc = ev->key.keysym.scancode;
            if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_Y || sc == SDL_SCANCODE_X)
                prompt_answer(1);
            else if (sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_N ||
                     sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_Z)
                prompt_answer(0);
            return !(sc == SDL_SCANCODE_Q && (ev->key.keysym.mod & KMOD_CTRL));
        }
        if (key_action(ev->key.keysym.scancode, (SDL_Keymod)ev->key.keysym.mod))
            return 1;
        if (paused && !(ev->key.keysym.mod & KMOD_CTRL) &&
            bindings_for_key(bindings_current(), ev->key.keysym.scancode) == ACTION_NONE) {
            pause_clear(paused);   /* any other key goes on */
            return 1;
        }
        return 0;
    case SDL_CONTROLLERBUTTONDOWN:
        if (paused & P_PROMPT) {
            if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_A)
                prompt_answer(1);
            else if (ev->cbutton.button == SDL_CONTROLLER_BUTTON_B)
                prompt_answer(0);
            return 1;
        }
        if (paused && bindings_for_button(bindings_current(),
                                          (SDL_GameControllerButton)ev->cbutton.button) ==
                          ACTION_NONE) {
            pause_clear(paused);
            return 1;
        }
        return 0;
    case SDL_CONTROLLERDEVICEREMOVED:
        if (set.pause_pad == ON && !(paused & P_PROMPT)) {
            pause_set(P_PAD);
            fprintf(stderr, "ct_sdl: a controller went: paused\n");
        }
        return 0;
    case SDL_WINDOWEVENT:
        /* Not at once: windows flick out of focus and back as they appear
           (and under compositors that move it around). */
        if (ev->window.event == SDL_WINDOWEVENT_FOCUS_LOST && focus_pause) {
            focus_pending = 1;
            focus_since = SDL_GetTicks();
            focus_win = SDL_GetWindowFromID(ev->window.windowID);
        } else if (ev->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            focus_pending = 0;
            if (focus_lost) {
                focus_lost = 0;
                pause_clear(P_FOCUS);
            }
        }
        return 0;
    case SDL_APP_WILLENTERBACKGROUND:
        sleeping(1);
        return 0;
    case SDL_APP_DIDENTERFOREGROUND:
        sleeping(0);
        return 0;
    default:
        return 0;
    }
}

/* ---- the frame ---- */

static void run_action(const char *a)
{
    fprintf(stderr, "ct_sdl: frame %ld: action %s\n", sched_frame_count(), a);
    if (!strcmp(a, "quicksave"))
        qol_quicksave();
    else if (!strcmp(a, "quickload"))
        qol_quickload();
    else if (!strncmp(a, "save", 4) && a[4] >= '1' && a[4] <= '4')
        qol_save_slot(a[4] - '0');
    else if (!strncmp(a, "load", 4) && a[4] >= '1' && a[4] <= '4')
        qol_load_slot(a[4] - '0');
    else if (!strcmp(a, "screenshot"))
        qol_screenshot();
    else if (!strcmp(a, "ffwd"))
        qol_ffwd_toggle();
    else if (!strcmp(a, "pause"))
        qol_pause_toggle();
    else if (!strcmp(a, "unpad"))   /* as a controller going */
        pause_set(P_PAD);
    else if (!strcmp(a, "sleep")) {
        sleeping(1);
        sleeping(0);
    } else if (!strcmp(a, "resume-yes"))
        prompt_answer(1);
    else if (!strcmp(a, "resume-no"))
        prompt_answer(0);
    else if (!strcmp(a, "always-run"))
        qol_always_run_toggle();
    else if (!strcmp(a, "crt"))
        qol_crt_toggle();
    else if (!strncmp(a, "turbo", 5))
        qol_turbo_toggle(PAD_A);
    else if (!strncmp(a, "press:", 6) && action_parse(a + 6) > 0)   /* as a key or button would */
        actions_emit((ct_action)action_parse(a + 6), 1);
    else if (!strncmp(a, "release:", 8) && action_parse(a + 8) > 0)
        actions_emit((ct_action)action_parse(a + 8), 0);
    else
        fprintf(stderr, "ct_sdl: unknown --qol-action %s\n", a);
}

static void actions_at(long f)
{
    for (int k = 0; k < n_actions; k++)
        if (actions[k].frame == f) {
            actions[k].frame = -1;
            run_action(actions[k].name);
        }
}

static void focus_check(void)
{
    if (!focus_pending || SDL_GetTicks() - focus_since < 300)
        return;
    focus_pending = 0;
    if (focus_win && (SDL_GetWindowFlags(focus_win) & SDL_WINDOW_INPUT_FOCUS))
        return;
    focus_lost = 1;
    pause_set(P_FOCUS);
}

int qol_frame(long f)
{
    frames_run++;
    focus_check();
    battery_check();
    gap_check();
    if (f % 6 == 0)
        sleep_poll();
    int speed = qol_speed();
    if (speed > 1)
        return f % speed == 0;
    if (on_batt && set.battery == BATT_SAVER)
        return (f & 1) == 0;   /* 30 Hz picture, 60 Hz game */
    return 1;
}

/* Between frames: the time for save states and screenshots. */
static void pending(void)
{
    if (want_load >= 0) {
        int s = want_load;
        want_load = want_save = -1;   /* a load beats a save asked at the same time */
        do_load(s);
    }
    if (want_save >= 0) {
        if (!do_save(want_save, 0))
            want_save = -1;
        else if (sched_frame_count() - want_since > 300) {
            qol_osd("Cannot save a state now");
            want_save = -1;
        }
    }
    if (want_shot) {
        want_shot = 0;
        do_shot();
    }
    if (want_auto && !do_save(SLOT_AUTO, 1)) {
        want_auto = 0;
        fprintf(stderr, "ct_sdl: resume point saved (sleep)\n");
        sleep_release();
    }
}

void qol_after_frame(void)
{
    if (!inited)
        return;
    actions_at(sched_frame_count());
    pending();
}

void qol_idle(void)
{
    if (host.poll)
        host.poll();
    actions_at(sched_frame_count());   /* (scripted: the frame the pause began on) */
    pending();
    if (!paused)
        return;
    if (!pause_shown || idle_ticks % 90 == 0) {   /* keep the message up */
        if (!pause_shown) {
            char plain[128];
            osd_text_plain(pause_text(), plain, sizeof plain);
            fprintf(stderr, "ct_sdl: %s\n", plain);
        }
        pause_shown = 1;
        if (hd2d)
            hd2d_osd(pause_text());
        else
            osd_sdl_show(pause_text());
    }
    idle_ticks++;
    sleep_poll();
    if (host.present)
        host.present();
    SDL_Delay(16);
}

/* ---- the plain picture ---- */

void qol_picture_rect(int w, int h, int *x, int *y, int *pw, int *ph)
{
    if (set.integer) {
        int s = w / SCHED_WIDTH < h / SCHED_HEIGHT ? w / SCHED_WIDTH : h / SCHED_HEIGHT;
        if (s < 1)
            s = 1;
        *pw = SCHED_WIDTH * s;
        *ph = SCHED_HEIGHT * s;
    } else if ((long)w * SCHED_HEIGHT > (long)h * SCHED_WIDTH) {
        *ph = h;
        *pw = h * SCHED_WIDTH / SCHED_HEIGHT;
    } else {
        *pw = w;
        *ph = w * SCHED_HEIGHT / SCHED_WIDTH;
    }
    *x = (w - *pw) / 2;
    *y = (h - *ph) / 2;
}

/* The CRT look over the picture: scanlines and an aperture-grille mask
   (one texture, multiplied in), and a glow (the frame again, smoothly
   scaled, a little larger, added at low strength). Drawn with
   SDL_Renderer blend modes, so it works on every SDL backend; the glow
   needs a custom blend mode and is left out where there is none. */
static SDL_Texture *mask;
static SDL_Renderer *mask_ren;
static int mask_w, mask_h;

static SDL_Texture *crt_mask(SDL_Renderer *ren, int pw, int ph)
{
    if (mask && mask_ren == ren && mask_w == pw && mask_h == ph)
        return mask;
    if (mask)
        SDL_DestroyTexture(mask);
    mask = NULL;
    uint32_t *px = malloc((size_t)pw * ph * 4);
    if (!px)
        return NULL;
    int s = ph / SCHED_HEIGHT;   /* output rows per line */
    for (int y = 0; y < ph; y++) {
        /* the line's last row is dark (a beam's gap), its middle full */
        int r = s ? y % s : 0;
        int scan = s >= 3 ? (r == s - 1 ? 150 : r == 0 ? 225 : 255) : s == 2 ? (r ? 175 : 255) : 255;
        for (int x = 0; x < pw; x++) {
            int c = x % 3, rgb[3] = {205, 205, 205};
            rgb[c] = 255;   /* R, G, B stripes */
            px[(size_t)y * pw + x] = 0xFF000000u | (uint32_t)(rgb[0] * scan / 255) << 16 |
                                     (uint32_t)(rgb[1] * scan / 255) << 8 |
                                     (uint32_t)(rgb[2] * scan / 255);
        }
    }
    mask = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, pw, ph);
    if (mask) {
        SDL_UpdateTexture(mask, NULL, px, pw * 4);
        SDL_SetTextureBlendMode(mask, SDL_BLENDMODE_MOD);
    }
    free(px);
    mask_ren = ren;
    mask_w = pw;
    mask_h = ph;
    return mask;
}

void qol_plain_draw(SDL_Renderer *ren, SDL_Texture *frame, int w, int h)
{
    SDL_Rect r;
    qol_picture_rect(w, h, &r.x, &r.y, &r.w, &r.h);
    SDL_RenderCopy(ren, frame, NULL, &r);
    if (!set.crt)
        return;
    static int no_glow;
    static SDL_BlendMode add;
    if (!add)
        add = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE,
                                         SDL_BLENDOPERATION_ADD, SDL_BLENDFACTOR_ZERO,
                                         SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    if (!no_glow) {
        SDL_BlendMode was;
        SDL_GetTextureBlendMode(frame, &was);
        if (SDL_SetTextureBlendMode(frame, add)) {
            no_glow = 1;
        } else {
            int g = r.h / SCHED_HEIGHT + 1;
            SDL_Rect big = {r.x - g, r.y - g / 2, r.w + 2 * g, r.h + g};
            SDL_SetTextureScaleMode(frame, SDL_ScaleModeLinear);
            SDL_SetTextureColorMod(frame, 52, 52, 52);
            SDL_RenderCopy(ren, frame, NULL, &big);
            SDL_SetTextureColorMod(frame, 255, 255, 255);
            SDL_SetTextureScaleMode(frame, SDL_ScaleModeNearest);
            SDL_SetTextureBlendMode(frame, was);
        }
    }
    SDL_Texture *m = crt_mask(ren, r.w, r.h);
    if (m)
        SDL_RenderCopy(ren, m, NULL, &r);
}

int qol_plain_shot(SDL_Renderer *ren, SDL_Texture *frame, int w, int h, const char *path)
{
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    uint8_t *buf = malloc((size_t)w * h * 4);
    int r = -1;
    if (t && buf && !SDL_SetRenderTarget(ren, t)) {
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        qol_plain_draw(ren, frame, w, h);
        r = SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, buf, w * 4) ? -1 : 0;
        SDL_SetRenderTarget(ren, NULL);
        if (!r)
            r = png_write_bgrx(path, buf, w, h);
    }
    free(buf);
    if (t)
        SDL_DestroyTexture(t);
    return r;
}

/* ---- port: the menu's rows (settings.h; frontend/menu.h) ----

   settings.ini stays qol_setting's and qol_settings_write's: the rows
   have no key, and a change saves through host.save_settings (the
   registry's saver, main.c's settings_save, which writes ours). */

#define BOTH(field, v) (set.field = (v), saved.field = set.field)

static int q_states(void *u) { (void)u; return states_on; }
static int q_plain(void *u) { (void)u; return !hd2d; }

/* Save state slots: 0 quick, 1-4, SLOT_AUTO the resume point. */
static int slot_exists(int s)
{
    char p[4200];
    slot_path(p, sizeof p, s, "state");
    return !access(p, F_OK);
}

static int q_can_load(void *u) { return states_on && !recording && slot_exists((int)(intptr_t)u); }

static const char *q_slot_text(void *u, char *buf, size_t n)
{
    char p[4200];
    struct stat st;
    slot_path(p, sizeof p, (int)(intptr_t)u, "state");
    if (!states_on)
        snprintf(buf, n, "Off (--no-save)");
    else if (want_save == (int)(intptr_t)u)   /* not possible at this frame: soon after */
        snprintf(buf, n, "Saved as the game goes on");
    else if (stat(p, &st))
        snprintf(buf, n, "Empty");
    else
        strftime(buf, n, "%b %d, %H:%M", localtime(&st.st_mtime));
    return buf;
}

/* The slot's picture, read again when the file changes. */
static const uint8_t *q_thumb(void *u, int *w, int *h, unsigned *gen)
{
    static struct { time_t mtime; off_t size; int w, h; uint8_t *bgrx; } cache[SLOT_AUTO + 1];
    int s = (int)(intptr_t)u;
    char p[4200];
    struct stat st;
    slot_path(p, sizeof p, s, "png");
    if (!states_on || stat(p, &st) || !slot_exists(s))
        return NULL;
    if (!cache[s].bgrx || cache[s].mtime != st.st_mtime || cache[s].size != st.st_size) {
        int iw, ih;
        uint8_t *rgba = pngio_read_rgba(p, &iw, &ih, NULL);
        free(cache[s].bgrx);
        cache[s].bgrx = NULL;
        if (!rgba)
            return NULL;
        for (int k = 0; k < iw * ih; k++) {   /* R G B A -> B G R x, in place */
            uint8_t r = rgba[k * 4];
            rgba[k * 4] = rgba[k * 4 + 2];
            rgba[k * 4 + 2] = r;
        }
        cache[s].bgrx = rgba;
        cache[s].w = iw, cache[s].h = ih;
        cache[s].mtime = st.st_mtime, cache[s].size = st.st_size;
    }
    *w = cache[s].w, *h = cache[s].h;
    *gen = (unsigned)cache[s].mtime ^ (unsigned)cache[s].size;
    return cache[s].bgrx;
}

static void q_load(void *u) { qol_load_slot((int)(intptr_t)u); }
static void q_save(void *u)
{
    int s = (int)(intptr_t)u;
    if (s >= 1 && s <= 4)
        slot = s;   /* the slot Shift + F6 / F9 use from now on */
    qol_save_slot(s);
}
static int q_get_resume(void *u) { (void)u; return set.resume == ON; }
static void q_set_resume(void *u, int v) { (void)u; BOTH(resume, v ? ON : OFF); }
static void q_shot(void *u) { (void)u; qol_screenshot(); }
static const char *q_pictures(void *u, char *buf, size_t n)
{
    (void)u;
    qol_pictures_dir(buf, n);
    return buf;
}

/* Display */
static int q_get_integer(void *u) { (void)u; return set.integer; }
static void q_set_integer(void *u, int v) { (void)u; BOTH(integer, v); }
static int q_get_crt(void *u) { (void)u; return set.crt; }
static void q_set_crt(void *u, int v) { (void)u; BOTH(crt, v); }
static const char *const batt_labels[3] = {"Off", "Light", "Saver"};
static int q_get_batt(void *u) { (void)u; return set.battery; }
static void q_set_batt(void *u, int v)
{
    (void)u;
    BOTH(battery, v);
    batt_checked = 0;   /* look again at the next frame */
}

/* Controls */
static int q_get_speed(void *u) { (void)u; return set.ffwd_speed; }
static void q_set_speed(void *u, int v) { (void)u; BOTH(ffwd_speed, v); }
static int q_get_ffwd(void *u) { (void)u; return ffwd_toggle; }
static void q_set_ffwd(void *u, int v)
{
    (void)u;
    if (!v != !ffwd_toggle)
        qol_ffwd_toggle();
}
static int q_get_run(void *u) { (void)u; return set.always_run; }
static void q_set_run(void *u, int v)
{
    (void)u;
    BOTH(always_run, v);
    sched_set_pref(SCHED_PREF_ALWAYS_RUN, set.always_run);
}
static const uint16_t turbo_bits[6] = {PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R};
static int q_get_turbo(void *u) { return (set.turbo & turbo_bits[(intptr_t)u]) != 0; }
static void q_set_turbo(void *u, int v)
{
    uint16_t b = turbo_bits[(intptr_t)u];
    BOTH(turbo, (uint16_t)(v ? set.turbo | b : set.turbo & ~b));
}
static const char *const tri_labels[3] = {"Auto", "On", "Off"};
static int q_get_pfocus(void *u) { (void)u; return set.pause_focus; }
static void q_set_pfocus(void *u, int v)
{
    (void)u;
    BOTH(pause_focus, v);
    focus_pause = set.pause_focus == AUTO ? !game_mode : set.pause_focus == ON;
}
static int q_get_ppad(void *u) { (void)u; return set.pause_pad == ON; }
static void q_set_ppad(void *u, int v) { (void)u; BOTH(pause_pad, v ? ON : OFF); }

void qol_preset_chosen(void) { user_preset = -1; }

void qol_register_settings(void)
{
    static const char *const loads[] = {"Quick slot", "Slot 1", "Slot 2", "Slot 3", "Slot 4",
                                        "Resume point"};
    settings_register(&(setting){.label = "Load", .category = "Save states", .type = SETTING_INFO,
                                 .flags = SETTING_HEADING});
    for (int s = 0; s <= SLOT_AUTO; s++)
        settings_register(&(setting){
            .label = loads[s], .category = "Save states", .type = SETTING_ACTION,
            .text = q_slot_text, .run = q_load, .enabled = q_can_load, .preview = q_thumb,
            .user = (void *)(intptr_t)s, .flags = SETTING_CLOSE,
            .help = s == SLOT_AUTO ? "Where you were when you last quit."
                                   : "Load it and play on. (F9 the quick slot; Shift+F9 the "
                                     "slot picked with F7)"});
    static const char *const saves[] = {"Save to the quick slot", "Save to slot 1",
                                        "Save to slot 2", "Save to slot 3", "Save to slot 4"};
    settings_register(&(setting){.label = "Save", .category = "Save states", .type = SETTING_INFO,
                                 .flags = SETTING_HEADING});
    for (int s = 0; s <= 4; s++)
        settings_register(&(setting){
            .label = saves[s], .category = "Save states", .type = SETTING_ACTION,
            .text = q_slot_text, .run = q_save, .enabled = q_states, .preview = q_thumb,
            .user = (void *)(intptr_t)s, .flags = SETTING_CONFIRM,
            .help = "The whole game as it is now. Press twice: it replaces what is there. (F6)"});
    settings_register(&(setting){.label = "", .category = "Save states", .type = SETTING_INFO,
                                 .flags = SETTING_HEADING});
    settings_register(&(setting){
        .label = "Resume where you left off", .category = "Save states", .type = SETTING_BOOL,
        .get = q_get_resume, .set = q_set_resume, .enabled = q_states,
        .help = "On quitting, the game is kept; the next start offers to go on from there."});
    settings_register(&(setting){.label = "Take a screenshot", .category = "Save states",
                                 .type = SETTING_ACTION, .run = q_shot,
                                 .help = "The picture without this menu, as a PNG. (F8)"});
    settings_register(&(setting){.label = "Screenshots folder", .category = "Save states",
                                 .type = SETTING_INFO, .text = q_pictures});

    settings_register(&(setting){
        .label = "Integer scale", .category = "Display", .type = SETTING_BOOL,
        .get = q_get_integer, .set = q_set_integer, .enabled = q_plain,
        .help = "The original picture in whole multiples, sharp; off: as large as fits."});
    settings_register(&(setting){.label = "CRT look", .category = "Display", .type = SETTING_BOOL,
                                 .get = q_get_crt, .set = q_set_crt, .enabled = q_plain,
                                 .help = "Scanlines and a soft glow over the original picture. "
                                         "(Ctrl+R)"});
    settings_register(&(setting){
        .label = "Battery mode", .category = "Display", .type = SETTING_ENUM,
        .options = batt_labels, .count = 3, .get = q_get_batt, .set = q_set_batt,
        .help = "On battery: Light, the lighter HD-2D look; Saver, also a 30 Hz picture."});

    settings_register(&(setting){.label = "Speed and turbo", .category = "Controls",
                                 .type = SETTING_INFO, .flags = SETTING_HEADING});
    settings_register(&(setting){.label = "Fast-forward", .category = "Controls",
                                 .type = SETTING_BOOL, .get = q_get_ffwd, .set = q_set_ffwd,
                                 .flags = SETTING_NO_SAVE,
                                 .help = "On until turned off (the ` key); hold L5 or Tab for "
                                         "a moment's worth."});
    settings_register(&(setting){.label = "Fast-forward speed", .category = "Controls",
                                 .type = SETTING_INT, .min = 2, .max = 4, .step = 1,
                                 .format = "%dx", .get = q_get_speed, .set = q_set_speed});
    settings_register(&(setting){.label = "Always run", .category = "Controls",
                                 .type = SETTING_BOOL, .get = q_get_run, .set = q_set_run,
                                 .help = "Run without holding the button; hold it to walk. "
                                         "(Caps Lock)"});
    static const char *const turbo_labels[6] = {"Turbo A", "Turbo B", "Turbo X", "Turbo Y",
                                                "Turbo L", "Turbo R"};
    for (int k = 0; k < 6; k++)
        settings_register(&(setting){
            .label = turbo_labels[k], .category = "Controls", .type = SETTING_BOOL,
            .get = q_get_turbo, .set = q_set_turbo, .user = (void *)(intptr_t)k,
            .help = "Held, the button presses itself 15 times a second. (Ctrl+T toggles the "
                    "buttons held)"});
    settings_register(&(setting){.label = "Pausing", .category = "Controls", .type = SETTING_INFO,
                                 .flags = SETTING_HEADING});
    settings_register(&(setting){
        .label = "Pause in the background", .category = "Controls", .type = SETTING_ENUM,
        .options = tri_labels, .count = 3, .get = q_get_pfocus, .set = q_set_pfocus,
        .help = "When another window has the focus. Auto: on, except in Steam's Game Mode."});
    settings_register(&(setting){.label = "Pause when a controller goes", .category = "Controls",
                                 .type = SETTING_BOOL, .get = q_get_ppad, .set = q_set_ppad,
                                 .help = "Unplugged, or out of battery."});
}
