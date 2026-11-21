/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * First run of an engine-only build (docs/MODULES.md): find the user's
 * ROM, check it against the game's game.toml, build the game module from
 * it if there is none (or it is stale), then load it. Status screens are
 * drawn by the caller from SCHED_WIDTH x SCHED_HEIGHT frames.
 *
 * Files, in the per-game preferences folder (SDL_GetPrefPath("ct-recomp",
 * GAME_ID)):
 *   ct_game.so     the module (tools/build_module.py --out); ct_game.dll on Windows
 *   ct_game.stamp  rom_sha1=HEX and module_abi=N: what it was built from
 *   rom_path       the last ROM that passed the check
 *
 * The game's files come from DATA_DIR/game/GAME_ID, the build tools from
 * DATA_DIR/tools and DATA_DIR/recomp. */
#ifndef CT_FIRSTRUN_H
#define CT_FIRSTRUN_H

#include <stdint.h>

typedef struct {
    const char *rom_arg;    /* --rom, or NULL */
    const char *data_dir;   /* where game/ and tools/ are */
    const char *game_id;    /* the directory name under DATA_DIR/game */
    int want_module;        /* 0: only find and check the ROM */
    /* Show a SCHED_WIDTH x SCHED_HEIGHT B G R x frame in the window (or
       NULL: no window). */
    void (*present)(const uint8_t *bgrx);
    /* Write the same frame to a PNG (the --shot status:PATH option). */
    void (*shot)(const uint8_t *bgrx, const char *path);
    const char *shot_path;  /* or NULL */
    /* No one to ask: without a usable ROM, or when the module can't be
       built or loaded, return FIRSTRUN_ERROR instead of waiting for a
       dropped file or offering the interpreter (--build-only). With
       rom_arg set, only that ROM is tried. */
    int build_only;
} firstrun_opts;

enum { FIRSTRUN_MODULE, FIRSTRUN_INTERP, FIRSTRUN_QUIT, FIRSTRUN_ERROR };

/* Runs after SDL_Init and window creation. On FIRSTRUN_MODULE and
   FIRSTRUN_INTERP the ROM is loaded (bus_init) and, for the first, the
   module installed (ct_module_load); FIRSTRUN_QUIT: the user closed the
   window; FIRSTRUN_ERROR: a setup problem, printed to stderr. */
int firstrun(const firstrun_opts *o);

#ifdef _WIN32
/* Windows: call first thing in main. With no valid standard handles (a GUI
   program started from the Explorer), stdout and stderr go to ct_sdl.log in
   the preferences folder; with a console or redirection they are left alone. */
void firstrun_log_redirect(const char *game_id);
#endif

#endif
