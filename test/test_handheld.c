/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Handheld controls (docs/HANDHELD.md) through a real SDL game controller:
 * an SDL virtual joystick shaped like the Xbox-style pad Steam Input
 * presents for a Steam Deck or ROG Ally, opened with SDL_GameControllerOpen.
 * Checks the SNES mapping (input_controller), the View+Menu quit combo
 * (quit_combo_*) and the device checks (device.h). No ROM, no window. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "device.h"
#include "input.h"
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

static SDL_Joystick *joy;
static SDL_GameController *pad;

static int pad_button(void *ctx, SDL_GameControllerButton b)
{
    return SDL_GameControllerGetButton(ctx, b);
}

static int pad_axis(void *ctx, SDL_GameControllerAxis a)
{
    return SDL_GameControllerGetAxis(ctx, a);
}

static void press(SDL_GameControllerButton b, int down)
{
    SDL_JoystickSetVirtualButton(joy, b, down ? SDL_PRESSED : SDL_RELEASED);
}

static void release_all(void)
{
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++)
        press(b, 0);
    for (int a = 0; a < SDL_CONTROLLER_AXIS_MAX; a++)
        SDL_JoystickSetVirtualAxis(joy, a, 0);
}

static uint16_t read_pad(void)
{
    SDL_GameControllerUpdate();
    return input_controller(pad_button, pad_axis, pad);
}

/* One frame of the frontend: the combo, then the pad unless masked. */
static int frame(quit_combo *q, long f, uint16_t *game)
{
    SDL_GameControllerUpdate();
    int r = quit_combo_update(q, f, pad_button, pad);
    *game = quit_combo_masks(q) ? 0 : input_controller(pad_button, pad_axis, pad);
    return r;
}

int main(void)
{
    if (SDL_Init(SDL_INIT_GAMECONTROLLER)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_VirtualJoystickDesc d;
    memset(&d, 0, sizeof d);
    d.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    d.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    d.naxes = SDL_CONTROLLER_AXIS_MAX;
    d.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
    d.vendor_id = 0x28de;    /* Valve: Steam Input's virtual pad */
    d.product_id = 0x11ff;
    d.name = "Steam Virtual Gamepad (test)";
    int idx = SDL_JoystickAttachVirtualEx(&d);
    CHECK(idx >= 0, "SDL_JoystickAttachVirtualEx: %s", SDL_GetError());
    if (idx < 0)
        return 1;
    CHECK(SDL_IsGameController(idx), "the virtual pad is a game controller");
    pad = SDL_GameControllerOpen(idx);
    joy = pad ? SDL_GameControllerGetJoystick(pad) : NULL;
    CHECK(pad && joy, "SDL_GameControllerOpen: %s", SDL_GetError());
    if (!pad)
        return 1;
    fprintf(stderr, "test_handheld: \"%s\"\n", SDL_GameControllerName(pad));

    /* The mapping, button by button: Deck/Ally labels -> SNES. */
    static const struct { SDL_GameControllerButton b; uint16_t bit; const char *what; } map[] = {
        {SDL_CONTROLLER_BUTTON_A, PAD_B, "A (bottom) = SNES B"},
        {SDL_CONTROLLER_BUTTON_B, PAD_A, "B (right) = SNES A"},
        {SDL_CONTROLLER_BUTTON_X, PAD_Y, "X (left) = SNES Y"},
        {SDL_CONTROLLER_BUTTON_Y, PAD_X, "Y (top) = SNES X"},
        {SDL_CONTROLLER_BUTTON_START, PAD_START, "Menu = Start"},
        {SDL_CONTROLLER_BUTTON_BACK, PAD_SELECT, "View = Select"},
        {SDL_CONTROLLER_BUTTON_LEFTSHOULDER, PAD_L, "L1 = L"},
        {SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, PAD_R, "R1 = R"},
        {SDL_CONTROLLER_BUTTON_DPAD_UP, PAD_UP, "d-pad up"},
        {SDL_CONTROLLER_BUTTON_DPAD_DOWN, PAD_DOWN, "d-pad down"},
        {SDL_CONTROLLER_BUTTON_DPAD_LEFT, PAD_LEFT, "d-pad left"},
        {SDL_CONTROLLER_BUTTON_DPAD_RIGHT, PAD_RIGHT, "d-pad right"},
        {SDL_CONTROLLER_BUTTON_LEFTSTICK, 0, "L3: no SNES button (aspect)"},
        {SDL_CONTROLLER_BUTTON_RIGHTSTICK, 0, "R3: no SNES button (look)"},
        {SDL_CONTROLLER_BUTTON_GUIDE, 0, "Steam / guide: not the game's"},
    };
    release_all();
    CHECK(read_pad() == 0, "idle pad: $%04X", read_pad());
    for (size_t k = 0; k < sizeof map / sizeof *map; k++) {
        press(map[k].b, 1);
        uint16_t got = read_pad();
        CHECK(got == map[k].bit, "%s: $%04X, expected $%04X", map[k].what, got, map[k].bit);
        press(map[k].b, 0);
    }
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, -32000);
    CHECK(read_pad() == PAD_LEFT, "left stick left: $%04X", read_pad());
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTX, 0);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTY, 32000);
    CHECK(read_pad() == PAD_DOWN, "left stick down: $%04X", read_pad());
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_LEFTY, 0);
    SDL_JoystickSetVirtualAxis(joy, SDL_CONTROLLER_AXIS_RIGHTX, 32000);
    CHECK(read_pad() == 0, "right stick: not movement");
    release_all();

    /* The quit combo. */
    quit_combo q;
    uint16_t game;
    long f = 1000;
    quit_combo_init(&q);
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    CHECK(frame(&q, f++, &game) == QUIT_NONE && game == PAD_SELECT, "View alone is Select");
    press(SDL_CONTROLLER_BUTTON_START, 1);
    int r = QUIT_NONE, frames = 0;
    uint16_t leaked = 0;
    while (r == QUIT_NONE && frames < 1000) {
        r = frame(&q, f++, &game);
        leaked |= game;
        frames++;
    }
    CHECK(r == QUIT_ASK, "holding View+Menu asks to quit (%d)", r);
    CHECK(frames == QUIT_HOLD_FRAMES + 1, "after 2 s: %d frames", frames);
    CHECK(!leaked, "View+Menu held: nothing reaches the game ($%04X)", leaked);
    press(SDL_CONTROLLER_BUTTON_BACK, 0);
    press(SDL_CONTROLLER_BUTTON_START, 0);
    for (int k = 0; k < 30; k++)
        CHECK(frame(&q, f++, &game) == QUIT_NONE && !game, "asking: waits, game masked");
    press(SDL_CONTROLLER_BUTTON_A, 1);
    CHECK(frame(&q, f++, &game) == QUIT_YES && !game, "A quits, and A doesn't reach the game");

    /* Cancel with B; the B press stays masked until released. */
    release_all();
    quit_combo_init(&q);
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    press(SDL_CONTROLLER_BUTTON_START, 1);
    for (r = QUIT_NONE, frames = 0; r == QUIT_NONE && frames < 1000; frames++)
        r = frame(&q, f++, &game);
    CHECK(r == QUIT_ASK, "asks again");
    release_all();
    frame(&q, f++, &game);
    press(SDL_CONTROLLER_BUTTON_B, 1);
    CHECK(frame(&q, f++, &game) == QUIT_CANCEL && !game, "B goes back to the game");
    CHECK(frame(&q, f++, &game) == QUIT_NONE && !game, "B still held: masked");
    press(SDL_CONTROLLER_BUTTON_B, 0);
    frame(&q, f++, &game);
    press(SDL_CONTROLLER_BUTTON_B, 1);
    CHECK(frame(&q, f++, &game) == QUIT_NONE && game == PAD_A, "a new B press is SNES A");
    release_all();

    /* A short View+Menu press does nothing, and nothing leaks. */
    frame(&q, f++, &game);
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    press(SDL_CONTROLLER_BUTTON_START, 1);
    leaked = 0;
    for (int k = 0; k < 60; k++) {
        CHECK(frame(&q, f++, &game) == QUIT_NONE, "1 s hold: no question");
        leaked |= game;
    }
    press(SDL_CONTROLLER_BUTTON_START, 0);
    frame(&q, f++, &game);
    leaked |= game;
    CHECK(!leaked, "early release: masked until both are up ($%04X)", leaked);
    press(SDL_CONTROLLER_BUTTON_BACK, 0);
    frame(&q, f++, &game);
    press(SDL_CONTROLLER_BUTTON_Y, 1);
    CHECK(frame(&q, f++, &game) == QUIT_NONE && game == PAD_X, "then input is back");
    release_all();

    /* No answer: the question times out. */
    quit_combo_init(&q);
    press(SDL_CONTROLLER_BUTTON_BACK, 1);
    press(SDL_CONTROLLER_BUTTON_START, 1);
    for (r = QUIT_NONE, frames = 0; r == QUIT_NONE && frames < 1000; frames++)
        r = frame(&q, f++, &game);
    release_all();
    for (r = QUIT_NONE, frames = 0; r == QUIT_NONE && frames < 2000; frames++)
        r = frame(&q, f++, &game);
    CHECK(r == QUIT_CANCEL && frames == QUIT_ASK_FRAMES, "timeout: %d after %d", r, frames);

    /* Device checks. */
    CHECK(device_product_kind("Jupiter") == DEVICE_STEAM_DECK, "Deck LCD");
    CHECK(device_product_kind("Galileo") == DEVICE_STEAM_DECK, "Deck OLED");
    CHECK(device_product_kind("ROG Ally RC71L_RC71L") == DEVICE_ROG_ALLY, "Ally");
    CHECK(device_product_kind("ROG Ally X RC72LA_RC72LA") == DEVICE_ROG_ALLY, "Ally X");
    CHECK(device_product_kind("Standard PC") == DEVICE_PC, "other");
    CHECK(device_renderer_is_deck("AMD Custom GPU 0405 (radeonsi, vangogh, ACO)"), "Deck GL");
    CHECK(device_renderer_is_deck("AMD VANGOGH (DRM 3.42.0)"), "Deck GL, older Mesa");
    CHECK(!device_renderer_is_deck("AMD Ryzen Z1 Extreme (radeonsi, phoenix, ACO)"), "Ally GL");
    unsetenv("GAMESCOPE_WAYLAND_DISPLAY");
    unsetenv("SteamDeck");
    unsetenv("SteamGamepadUI");
    unsetenv("XDG_CURRENT_DESKTOP");
    CHECK(!device_game_mode(NULL), "desktop");
    setenv("SteamDeck", "0", 1);
    CHECK(!device_game_mode(NULL), "SteamDeck=0");
    setenv("XDG_CURRENT_DESKTOP", "gamescope", 1);
    const char *why = NULL;
    CHECK(device_game_mode(&why) && !strcmp(why, "XDG_CURRENT_DESKTOP=gamescope"), "gamescope");
    setenv("SteamDeck", "1", 1);
    CHECK(device_game_mode(&why) && !strcmp(why, "SteamDeck=1"), "SteamDeck=1");

    SDL_GameControllerClose(pad);
    SDL_JoystickDetachVirtual(idx);
    SDL_Quit();
    printf("test_handheld: %ld checks, %ld failures\n", checks, fails);
    return fails ? 1 : 0;
}
