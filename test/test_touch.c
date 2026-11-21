/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * frontend/touch.c: touches to SNES pad bits. No ROM, no window: events
 * are built by hand and fed to touch_event. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "input.h"
#include "touch.h"
#include "platform.h"   /* port: Windows: mkdir with no mode, setenv */

static long checks, fails;
#define CHECK(c, ...)                                                    \
    do {                                                                 \
        checks++;                                                        \
        if (!(c)) {                                                      \
            fails++;                                                     \
            fprintf(stderr, "%s:%d: FAIL: ", __FILE__, __LINE__);        \
            fprintf(stderr, __VA_ARGS__);                                \
            fputc('\n', stderr);                                         \
        }                                                                \
    } while (0)

static int W = 1920, H = 1080;
static long frame;

static void finger(Uint32 type, int id, float x, float y)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = type;
    e.tfinger.touchId = 1;
    e.tfinger.fingerId = id;
    e.tfinger.x = x / W;
    e.tfinger.y = y / H;
    CHECK(touch_event(&e, frame) == 1, "finger event used");
}

static void at(const char *name, float *x, float *y)
{
    CHECK(touch_target(name, x, y) == 0, "target %s", name);
}

static void tick(long n)
{
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_USEREVENT;
    for (long k = 0; k < n; k++) {
        frame++;
        touch_event(&e, frame);
        touch_buttons(frame);
    }
}

int main(void)
{
    /* Keep any saved layout out of it: the user's, and one an earlier run
       saved from edit mode (a fixed TMPDIR would keep it). A fresh
       directory every run. */
    static char dir[4096];
    snprintf(dir, sizeof dir, "%s/test_touch.XXXXXX", ct_tmpdir());
    if (!mkdtemp(dir)) {
        perror("test_touch: mkdtemp");
        return 1;
    }
    setenv("XDG_DATA_HOME", dir, 1);
    touch_opts o = {1, 2};
    touch_init(&o);
    CHECK(touch_visible(), "--touch: shown from the start");
    touch_set_screen(NULL, W, H, touch_picture_plain(W, H));
    CHECK(touch_buttons(frame) == 0, "nothing held");

    float x, y, x2, y2;
    at("start", &x, &y);
    finger(SDL_FINGERDOWN, 1, x, y);
    CHECK(touch_buttons(frame) == PAD_START, "start: $%04X", touch_buttons(frame));
    finger(SDL_FINGERUP, 1, x, y);
    CHECK(touch_buttons(frame) == 0, "start released");

    /* D-pad: a diagonal, then sliding to a cardinal, held off the pad. */
    at("upright", &x, &y);
    finger(SDL_FINGERDOWN, 2, x, y);
    CHECK(touch_buttons(frame) == (PAD_UP | PAD_RIGHT), "up-right: $%04X", touch_buttons(frame));
    at("left", &x, &y);
    finger(SDL_FINGERMOTION, 2, x, y);
    CHECK(touch_buttons(frame) == PAD_LEFT, "slid left: $%04X", touch_buttons(frame));
    at("down", &x, &y);
    at("dpad", &x2, &y2);
    finger(SDL_FINGERMOTION, 2, x2 + (x - x2) * 3, y2 + (y - y2) * 3);
    CHECK(touch_buttons(frame) == PAD_DOWN, "down, beyond the pad's edge: $%04X", touch_buttons(frame));

    /* A second finger on A, a third on L: all held together. */
    at("a", &x, &y);
    finger(SDL_FINGERDOWN, 3, x, y);
    at("l", &x, &y);
    finger(SDL_FINGERDOWN, 4, x, y);
    CHECK(touch_buttons(frame) == (PAD_DOWN | PAD_A | PAD_L), "three fingers: $%04X", touch_buttons(frame));
    /* Slide the A finger to B. */
    at("b", &x, &y);
    finger(SDL_FINGERMOTION, 3, x, y);
    CHECK(touch_buttons(frame) == (PAD_DOWN | PAD_B | PAD_L), "slid A to B: $%04X", touch_buttons(frame));
    /* Halfway between A and B: both. */
    at("a", &x2, &y2);
    finger(SDL_FINGERMOTION, 3, (x + x2) / 2, (y + y2) / 2);
    CHECK((touch_buttons(frame) & (PAD_A | PAD_B)) == (PAD_A | PAD_B), "between A and B: $%04X",
          touch_buttons(frame));
    finger(SDL_FINGERUP, 2, 0, 0);
    finger(SDL_FINGERUP, 3, 0, 0);
    finger(SDL_FINGERUP, 4, 0, 0);
    CHECK(touch_buttons(frame) == 0, "all up");

    /* The mouse as one finger (--touch). */
    at("x", &x, &y);
    SDL_Event m;
    memset(&m, 0, sizeof m);
    m.type = SDL_MOUSEBUTTONDOWN;
    m.button.button = SDL_BUTTON_LEFT;
    m.button.x = (int)x;
    m.button.y = (int)y;
    CHECK(touch_event(&m, frame) == 1, "mouse down used");
    CHECK(touch_buttons(frame) == PAD_X, "mouse on X: $%04X", touch_buttons(frame));
    m.type = SDL_MOUSEBUTTONUP;
    touch_event(&m, frame);
    CHECK(touch_buttons(frame) == 0, "mouse up");

    /* Hides after the timeout (2 s) with no touch; a touch shows it. */
    tick(60);
    CHECK(touch_visible(), "still shown after 1 s");
    tick(80);
    CHECK(!touch_visible(), "hidden after 2 s");
    at("y", &x, &y);
    finger(SDL_FINGERDOWN, 5, x, y);
    CHECK(touch_visible() && touch_buttons(frame) == PAD_Y, "a touch shows it and acts");
    finger(SDL_FINGERUP, 5, x, y);

    /* A controller hides it; its events are not used up. */
    SDL_Event c;
    memset(&c, 0, sizeof c);
    c.type = SDL_CONTROLLERBUTTONDOWN;
    CHECK(touch_event(&c, frame) == 0, "controller event passed on");
    CHECK(!touch_visible(), "hidden by the controller");

    /* Long press on an empty spot: edit mode, where nothing is pressed. */
    finger(SDL_FINGERDOWN, 6, W / 2.0f, H / 2.0f);
    tick(60);
    finger(SDL_FINGERUP, 6, W / 2.0f, H / 2.0f);
    at("start", &x, &y);
    finger(SDL_FINGERDOWN, 7, x, y);
    CHECK(touch_buttons(frame) == 0, "edit mode: start not pressed");
    finger(SDL_FINGERMOTION, 7, x - 100, y - 50);   /* moves the Start button */
    finger(SDL_FINGERUP, 7, x - 100, y - 50);
    finger(SDL_FINGERDOWN, 8, W / 2.0f, H / 2.0f);
    tick(60);
    finger(SDL_FINGERUP, 8, W / 2.0f, H / 2.0f);
    touch_set_screen(NULL, W, H, touch_picture_plain(W, H));   /* the next frame's layout */
    at("start", &x2, &y2);
    CHECK(x2 < x - 90 && y2 < y - 40, "start moved to %.0f,%.0f from %.0f,%.0f", x2, y2, x, y);
    finger(SDL_FINGERDOWN, 9, x2, y2);
    CHECK(touch_buttons(frame) == PAD_START, "edit mode left: start at its new place");
    finger(SDL_FINGERUP, 9, x2, y2);

    /* The overlay tessellates into whole triangles. */
    const SDL_Vertex *v;
    int n = touch_geometry_for(1080, 2400, touch_picture_aspect(1080, 2400, 4.0 / 3.0), &v);
    CHECK(n > 0 && n % 3 == 0, "portrait geometry: %d vertices", n);

    /* The quick menu: the handle at the top edge's middle, and three fingers. */
    touch_menu_requested();
    tick(1);
    finger(SDL_FINGERDOWN, 20, W / 2.0f, 6);
    finger(SDL_FINGERUP, 20, W / 2.0f, 6);
    CHECK(touch_menu_requested() == 1, "a tap on the handle asks for the quick menu");
    CHECK(touch_menu_requested() == 0, "asked once");
    finger(SDL_FINGERDOWN, 20, W / 2.0f, 6);
    finger(SDL_FINGERMOTION, 20, W / 2.0f + 300, 200);
    finger(SDL_FINGERUP, 20, W / 2.0f + 300, 200);
    CHECK(touch_menu_requested() == 0, "a drag from the handle is not a tap");
    tick(250);   /* idle: the handle has faded (and the overlay is hidden) */
    finger(SDL_FINGERDOWN, 20, W / 2.0f, 6);
    finger(SDL_FINGERUP, 20, W / 2.0f, 6);
    CHECK(touch_menu_requested() == 0, "a faded handle does not act");
    tick(1);
    finger(SDL_FINGERDOWN, 20, W / 2.0f, 6);
    finger(SDL_FINGERUP, 20, W / 2.0f, 6);
    CHECK(touch_menu_requested() == 1, "the handle is back after a touch");
    {   /* never over the on-screen controller: the narrow layout of a phone */
        touch_set_screen(NULL, 1080, 2400, touch_picture_aspect(1080, 2400, 4.0 / 3.0));
        const SDL_Vertex *gv;
        int n1 = touch_geometry(&gv);
        CHECK(n1 > 0 && n1 % 3 == 0, "handle drawn with the controls: %d vertices", n1);
        touch_set_screen(NULL, W, H, touch_picture_plain(W, H));
    }
    /* three fingers on empty spots, together, lifted soon */
    finger(SDL_FINGERDOWN, 31, W / 2.0f - 60, H / 2.0f);
    finger(SDL_FINGERDOWN, 32, W / 2.0f, H / 2.0f);
    finger(SDL_FINGERDOWN, 33, W / 2.0f + 60, H / 2.0f);
    tick(6);
    finger(SDL_FINGERUP, 31, W / 2.0f - 60, H / 2.0f);
    CHECK(touch_menu_requested() == 1, "a three-finger tap asks for the quick menu");
    CHECK(touch_buttons(frame) == 0, "no finger is left on the controls");
    /* ... not when held long, moved, or on a control */
    finger(SDL_FINGERDOWN, 31, W / 2.0f - 60, H / 2.0f);
    finger(SDL_FINGERDOWN, 32, W / 2.0f, H / 2.0f);
    finger(SDL_FINGERDOWN, 33, W / 2.0f + 60, H / 2.0f);
    tick(40);
    finger(SDL_FINGERUP, 31, W / 2.0f - 60, H / 2.0f);
    finger(SDL_FINGERUP, 32, W / 2.0f, H / 2.0f);
    finger(SDL_FINGERUP, 33, W / 2.0f + 60, H / 2.0f);
    CHECK(touch_menu_requested() == 0, "three fingers held long: no");
    finger(SDL_FINGERDOWN, 31, W / 2.0f - 60, H / 2.0f);
    finger(SDL_FINGERDOWN, 32, W / 2.0f, H / 2.0f);
    finger(SDL_FINGERDOWN, 33, W / 2.0f + 60, H / 2.0f);
    finger(SDL_FINGERMOTION, 33, W / 2.0f + 200, H / 2.0f + 100);
    finger(SDL_FINGERUP, 31, W / 2.0f - 60, H / 2.0f);
    finger(SDL_FINGERUP, 32, W / 2.0f, H / 2.0f);
    finger(SDL_FINGERUP, 33, W / 2.0f + 200, H / 2.0f + 100);
    CHECK(touch_menu_requested() == 0, "three fingers, one moved: no");
    at("a", &x, &y);
    finger(SDL_FINGERDOWN, 31, x, y);
    finger(SDL_FINGERDOWN, 32, W / 2.0f, H / 2.0f);
    finger(SDL_FINGERDOWN, 33, W / 2.0f + 60, H / 2.0f);
    finger(SDL_FINGERUP, 31, x, y);
    finger(SDL_FINGERUP, 32, W / 2.0f, H / 2.0f);
    finger(SDL_FINGERUP, 33, W / 2.0f + 60, H / 2.0f);
    CHECK(touch_menu_requested() == 0, "three fingers, one on a button: no");

    printf("test_touch: %ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
