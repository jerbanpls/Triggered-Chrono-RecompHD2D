/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the overlays' data (overlays.h). */
#include "overlays.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "qol.h"
#include "settings.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include "platform.h"   /* port: Windows: rename that replaces */

static int on[OVERLAY_COUNT], corner = OVERLAY_TR;

void overlays_set(int which, int v)
{
    if (which >= 0 && which < OVERLAY_COUNT)
        on[which] = v != 0;
}

int overlays_get(int which)
{
    return which >= 0 && which < OVERLAY_COUNT ? on[which] : 0;
}

void overlays_set_corner(int c)
{
    corner = c >= OVERLAY_TL && c <= OVERLAY_BR ? c : OVERLAY_TR;
}

int overlays_get_corner(void)
{
    return corner;
}

/* ---- frame time ---- */

#define RING 60
static double ring[RING];
static int ring_n, ring_at;

void overlays_note_frame(double ms)
{
    if (ms <= 0 || ms > 5000)
        return;   /* a stall (a menu, a suspended window) isn't a frame time */
    ring[ring_at] = ms;
    ring_at = (ring_at + 1) % RING;
    if (ring_n < RING)
        ring_n++;
}

double overlays_frame_ms(void)
{
    double sum = 0;
    for (int k = 0; k < ring_n; k++)
        sum += ring[k];
    return ring_n ? sum / ring_n : 0;
}

double overlays_frame_max_ms(void)
{
    double m = 0;
    for (int k = 0; k < ring_n; k++)
        if (ring[k] > m)
            m = ring[k];
    return m;
}

double overlays_fps(void)
{
    double ms = overlays_frame_ms();
    return ms > 0 ? 1000.0 / ms : 0;
}

/* ---- play time ---- */

static long play_frames, play_saved = -1, last_emu = -1;
static char play_path[4096];
static unsigned last_flush_ms;
static int play_hooked;

long overlays_playtime_frames(void) { return play_frames; }
void overlays_playtime_set(long f) { play_frames = f < 0 ? 0 : f; }

void overlays_playtime_flush(void)
{
    if (!play_path[0] || play_frames == play_saved)
        return;
    char tmp[sizeof play_path + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", play_path);
    FILE *f = fopen(tmp, "w");
    if (!f)
        return;
    int ok = fprintf(f, "frames=%ld\n", play_frames) > 0;
    ok = fflush(f) == 0 && ok;
    ok = fclose(f) == 0 && ok;
    if (ok && !rename(tmp, play_path))
        play_saved = play_frames;
    else
        remove(tmp);
}

static void flush_at_exit(void)
{
    overlays_playtime_flush();
}

void overlays_playtime_open(const char *file)
{
    snprintf(play_path, sizeof play_path, "%s", file);
    FILE *f = fopen(file, "r");
    long v;
    play_frames = 0;
    if (f && fscanf(f, "frames=%ld", &v) == 1 && v >= 0)
        play_frames = v;
    if (f)
        fclose(f);
    play_saved = play_frames;
    last_emu = -1;
    if (!play_hooked) {
        play_hooked = 1;
        atexit(flush_at_exit);
    }
}

void overlays_init(const char *game_id)
{
    char *dir = SDL_GetPrefPath("ct-recomp", game_id ? game_id : "game");
    if (!dir)
        return;
    char p[4096];
    snprintf(p, sizeof p, "%splaytime.txt", dir);
    SDL_free(dir);
    overlays_playtime_open(p);
}

void overlays_format_playtime(long frames, char *buf, size_t n)
{
    long s = frames / 60;
    snprintf(buf, n, "%ld:%02ld:%02ld", s / 3600, s / 60 % 60, s % 60);
}

void overlays_format_clock(time_t t, char *buf, size_t n)
{
    struct tm tm;
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

/* ---- battery: one function, a backend per platform ---- */

int overlays_battery_percent(void)
{
#if defined(_WIN32)
    /* port: Windows: GetSystemPowerStatus (BatteryLifePercent is 255 when
       unknown, ACLineStatus alone says nothing about a desktop). Not built
       or run on Linux: written from the API's documentation. */
    SYSTEM_POWER_STATUS s;
    if (GetSystemPowerStatus(&s) && s.BatteryLifePercent <= 100 && !(s.BatteryFlag & 128))
        return s.BatteryLifePercent;
    return -1;
#elif defined(__ANDROID__)
    /* port: Android: STUB. The charge comes from the Java side (the
       BatteryManager, android/ in the app): not wired yet, so no battery
       is reported. */
    return -1;
#else
    return qol_battery_percent("/sys/class/power_supply");
#endif
}

/* ---- the per-frame update ---- */

static int batt = -1;
static unsigned batt_at;
static int batt_polled;

void overlays_update(unsigned ms, long emu_frames)
{
    if (last_emu >= 0) {
        long d = emu_frames - last_emu;
        if (d > 0 && d <= 10)   /* a state load or a reset jumps: not play time */
            play_frames += d;
    }
    last_emu = emu_frames;
    if (play_path[0] && ms - last_flush_ms >= 60000u) {
        last_flush_ms = ms;
        overlays_playtime_flush();
    }
    if (on[OVERLAY_BATTERY] && (!batt_polled || ms - batt_at >= 15000u)) {
        batt_polled = 1;
        batt_at = ms;
        batt = overlays_battery_percent();
    }
}

int overlays_lines(char (*lines)[32], int max)
{
    int n = 0;
    if (on[OVERLAY_FPS] && n < max) {
        double ms = overlays_frame_ms();
        if (ms > 0)
            snprintf(lines[n++], 32, "%.0f FPS  %.1f ms", overlays_fps(), ms);
        else
            snprintf(lines[n++], 32, "-- FPS");
    }
    if (on[OVERLAY_CLOCK] && n < max)
        overlays_format_clock(time(NULL), lines[n++], 32);
    if (on[OVERLAY_BATTERY] && n < max) {
        if (batt >= 0)
            snprintf(lines[n++], 32, "Battery %d%%", batt);
        else
            snprintf(lines[n++], 32, "Battery n/a");
    }
    if (on[OVERLAY_PLAYTIME] && n < max) {
        char t[24];
        overlays_format_playtime(play_frames, t, sizeof t);
        snprintf(lines[n++], 32, "Play %s", t);
    }
    return n;
}

/* ---- settings ---- */

static int get_on(void *u) { return on[(intptr_t)u]; }
static void set_on(void *u, int v) { overlays_set((int)(intptr_t)u, v); }
static int get_corner(void *u) { (void)u; return corner; }
static void set_corner(void *u, int v) { (void)u; overlays_set_corner(v); }

void overlays_register_settings(void)
{
    static const char *const corners[4] = {"Top left", "Top right", "Bottom left",
                                           "Bottom right"},
                             *const corner_values[4] = {"tl", "tr", "bl", "br"};
    static const struct { const char *key, *label, *help; } rows[OVERLAY_COUNT] = {
        {"overlay_fps", "Show FPS", "Frames per second and the frame time in the corner."},
        {"overlay_clock", "Show the clock", "The time of day in the corner."},
        {"overlay_battery", "Show the battery", "The battery's charge in the corner."},
        {"overlay_playtime", "Show play time",
         "How long this game has been played (its unpaused frames), in the corner."},
    };
    settings_register(&(setting){.label = "Overlays", .category = "Display",
                                 .type = SETTING_INFO, .flags = SETTING_HEADING});
    for (int k = 0; k < OVERLAY_COUNT; k++)
        settings_register(&(setting){.key = rows[k].key, .label = rows[k].label,
                                     .category = "Display", .type = SETTING_BOOL,
                                     .get = get_on, .set = set_on, .user = (void *)(intptr_t)k,
                                     .help = rows[k].help});
    settings_register(&(setting){
        .key = "overlay_corner", .label = "Overlay corner", .category = "Display",
        .type = SETTING_ENUM, .options = corners, .values = corner_values, .count = 4,
        .get = get_corner, .set = set_corner, .help = "Where the overlays sit."});
}
