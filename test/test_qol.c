/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The frontend's quality-of-life helpers that need no game (frontend/qol.h):
 * the battery check against a fake /sys/class/power_supply, the pictures
 * folder, settings.ini round trips, turbo and the buttons kept from the
 * game after an unpause, and the plain picture's rectangle. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "harness.h"
#include "input.h"
#include "qol.h"
#include "platform.h"   /* port: Windows: mkdir with no mode, setenv */

static char tmp[4096];

/* sysfs's name has a colon, which Windows doesn't allow in a file name */
#ifdef _WIN32
#define UCSI_NAME "ucsi-source-psy-USBC000-001"
#else
#define UCSI_NAME "ucsi-source-psy-USBC000:001"
#endif

static void put(const char *dir, const char *name, const char *file, const char *text)
{
    char p[4300];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/%s/%s", dir, name, file);
    FILE *f = fopen(p, "w");
    CHECK(f != NULL, "f != NULL");
    fputs(text, f);
    fclose(f);
}

static void test_battery(void)
{
    char d[4200];
    snprintf(d, sizeof d, "%s/ac", tmp);
    mkdir(d, 0755);
    put(d, "BAT1", "type", "Battery\n");
    put(d, "BAT1", "status", "Charging\n");
    put(d, "ACAD", "type", "Mains\n");
    put(d, "ACAD", "online", "1\n");
    CHECK(qol_on_battery(d) == 0, "qol_on_battery(d) == 0");
    snprintf(d, sizeof d, "%s/batt", tmp);
    mkdir(d, 0755);
    put(d, "BAT1", "type", "Battery\n");
    put(d, "BAT1", "status", "Discharging\n");
    put(d, "ACAD", "type", "Mains\n");
    put(d, "ACAD", "online", "0\n");
    CHECK(qol_on_battery(d) == 1, "qol_on_battery(d) == 1");
    /* a USB-C charger (Steam Deck, Ally) counts as outside power */
    put(d, UCSI_NAME, "type", "USB\n");
    put(d, UCSI_NAME, "online", "1\n");
    CHECK(qol_on_battery(d) == 0, "qol_on_battery(d) == 0");
    /* a desktop: no battery at all */
    snprintf(d, sizeof d, "%s/desk", tmp);
    mkdir(d, 0755);
    CHECK(qol_on_battery(d) == 0, "qol_on_battery(d) == 0");
    CHECK(qol_on_battery("/nonexistent/power_supply") == 0, "qol_on_battery('/nonexistent/power_supply') == 0");
}

static void test_pictures(void)
{
    char out[4096], conf[4200];
    setenv("HOME", tmp, 1);
    setenv("XDG_PICTURES_DIR", "/pics", 1);
    qol_pictures_dir(out, sizeof out);
    CHECK(!strcmp(out, "/pics/ct-recomp"), "!strcmp(out, '/pics/ct-recomp')");
    unsetenv("XDG_PICTURES_DIR");
    setenv("XDG_CONFIG_HOME", tmp, 1);
    snprintf(conf, sizeof conf, "%s/user-dirs.dirs", tmp);
    FILE *f = fopen(conf, "w");
    CHECK(f != NULL, "f != NULL");
    fputs("# comment\nXDG_DESKTOP_DIR=\"$HOME/Desktop\"\nXDG_PICTURES_DIR=\"$HOME/Bilder\"\n", f);
    fclose(f);
    qol_pictures_dir(out, sizeof out);
    char want[4300];
    snprintf(want, sizeof want, "%s/Bilder/ct-recomp", tmp);
    CHECK(!strcmp(out, want), "!strcmp(out, want)");
    remove(conf);
    qol_pictures_dir(out, sizeof out);
    snprintf(want, sizeof want, "%s/Pictures/ct-recomp", tmp);
    CHECK(!strcmp(out, want), "!strcmp(out, want)");
}

static void test_settings(void)
{
    CHECK(qol_setting("ffwd_speed", "9") == 1, "qol_setting('ffwd_speed', '9') == 1");
    CHECK(qol_setting("turbo", "a,y") == 1, "qol_setting('turbo', 'a,y') == 1");
    CHECK(qol_setting("battery_mode", "saver") == 1, "qol_setting('battery_mode', 'saver') == 1");
    CHECK(qol_setting("crt", "on") == 1, "qol_setting('crt', 'on') == 1");
    CHECK(qol_setting("integer_scale", "off") == 1, "qol_setting('integer_scale', 'off') == 1");
    CHECK(qol_setting("preset", "deck") == 0, "qol_setting('preset', 'deck') == 0");   /* not ours */
    char path[4200], line[256], all[2048] = "";
    snprintf(path, sizeof path, "%s/settings.ini", tmp);
    FILE *f = fopen(path, "w+");
    CHECK(f != NULL, "f != NULL");
    qol_settings_write(f);
    rewind(f);
    while (fgets(line, sizeof line, f))
        strncat(all, line, sizeof all - strlen(all) - 1);
    fclose(f);
    CHECK(strstr(all, "ffwd_speed=4\n") != NULL, "strstr(all, 'ffwd_speed=4\n') != NULL");   /* clamped to 2-4 */
    CHECK(strstr(all, "turbo=a,y\n") != NULL, "strstr(all, 'turbo=a,y\n') != NULL");
    CHECK(strstr(all, "battery_mode=saver\n") != NULL, "strstr(all, 'battery_mode=saver\n') != NULL");
    CHECK(strstr(all, "crt=on\n") != NULL, "strstr(all, 'crt=on\n') != NULL");
    CHECK(strstr(all, "integer_scale=off\n") != NULL, "strstr(all, 'integer_scale=off\n') != NULL");
    CHECK(strstr(all, "resume=on\n") != NULL, "strstr(all, 'resume=on\n') != NULL");   /* defaults written too */
    CHECK(strstr(all, "always_run=off\n") != NULL, "strstr(all, 'always_run=off\n') != NULL");
}

static void test_buttons(void)
{
    qol_setting("turbo", "a");
    /* turbo A: pressed 2 frames, released 2 */
    int seen[8];
    for (long f = 0; f < 8; f++)
        seen[f] = (qol_buttons(0, PAD_A | PAD_B, f) & PAD_A) != 0;
    CHECK(seen[0] && seen[1] && !seen[2] && !seen[3] && seen[4] && seen[5] && !seen[6], "seen[0] && seen[1] && !seen[2] && !seen[3] && seen[4] && see");
    for (long f = 0; f < 8; f++)   /* B has no turbo */
        CHECK(qol_buttons(0, PAD_B, f) == PAD_B, "qol_buttons(0, PAD_B, f) == PAD_B");
    qol_setting("turbo", "");
    CHECK(qol_buttons(1, PAD_Y, 2) == PAD_Y, "qol_buttons(1, PAD_Y, 2) == PAD_Y");
    /* the button that ends a pause doesn't reach the game until let go */
    qol_pause_toggle();
    CHECK(qol_paused(), "paused");
    qol_pause_toggle();
    CHECK(!qol_paused(), "going on");
    CHECK(qol_buttons(0, PAD_A, 10) == 0, "held through the pause: kept");
    CHECK(qol_buttons(0, PAD_A | PAD_UP, 11) == PAD_UP, "a new one passes");
    CHECK(qol_buttons(0, 0, 12) == 0, "released");
    CHECK(qol_buttons(0, PAD_A, 13) == PAD_A, "pressed again: passes");
}

static void test_rect(void)
{
    int x, y, w, h;
    qol_setting("integer_scale", "on");
    qol_picture_rect(1280, 800, &x, &y, &w, &h);   /* Steam Deck: 3x, bars */
    CHECK(w == 768 && h == 672 && x == 256 && y == 64, "w == 768 && h == 672 && x == 256 && y == 64");
    qol_picture_rect(1920, 1080, &x, &y, &w, &h);  /* ROG Ally: 4x */
    CHECK(w == 1024 && h == 896 && x == 448 && y == 92, "w == 1024 && h == 896 && x == 448 && y == 92");
    qol_picture_rect(100, 100, &x, &y, &w, &h);    /* smaller than 1x: 1x */
    CHECK(w == 256 && h == 224, "w == 256 && h == 224");
    qol_setting("integer_scale", "off");
    qol_picture_rect(1280, 800, &x, &y, &w, &h);   /* fit: full height */
    CHECK(h == 800 && w == 914 && x == 183 && y == 0, "h == 800 && w == 914 && x == 183 && y == 0");
}

int main(void)
{
    snprintf(tmp, sizeof tmp, "%s/ct_test_qol_XXXXXX", ct_tmpdir());
    CHECK(mkdtemp(tmp) != NULL, "mkdtemp(tmp) != NULL");
    test_battery();
    test_pictures();
    test_settings();
    test_buttons();
    test_rect();
    char cmd[4200];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", tmp);
    if (system(cmd)) {
    }
    return th_report("qol");
}
