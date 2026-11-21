/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Static builds: install the tables linked into this program (a game's
 * generated ct_funcs.c and ct_overlays.c) before main runs, and the game's
 * scene description, rumble hook, music watcher and text if it has them
 * (module.h). */
#ifndef CT_TABLES_DEFINE   /* also set by the build */
#define CT_TABLES_DEFINE
#endif
#include "func_table.h"
#include "module.h"

#ifdef _WIN32
/* port: PE has no weak undefined references (the link fails on them), so
   a Windows static build links no game scene, rumble hook, music watcher,
   text or poses. Games reach a Windows engine as modules (module.c), whose
   entry point (module_entry.c) is WIN-2's. */
#define ct_game_scene NULL
#define ct_game_rumble NULL
#define ct_game_text NULL
#define ct_game_pose NULL
#define CT_NO_GAME_MUSIC
#else
__attribute__((weak)) int ct_game_scene(ct_scene *out);   /* NULL: none linked */
__attribute__((weak)) int ct_game_rumble(long frame);      /* rumble.h; NULL: none */
__attribute__((weak)) extern const ct_music_hooks ct_game_music;
__attribute__((weak)) int ct_game_text(ct_text *out);
__attribute__((weak)) int ct_game_pose(int obj, ct_scene_pose *out, uint8_t *rgba, int max);

#endif

__attribute__((constructor)) static void install_linked_tables(void)
{
    const ct_tables t = {ct_funcs, ct_func_count, ct_externs, ct_extern_count,
                         ct_jumptables, ct_jumptable_count, ct_overlay_funcs,
                         ct_overlay_func_count};
    ct_tables_install(&t);
    ct_scene_set_source(ct_game_scene);
    ct_rumble_set_source(ct_game_rumble);
#ifdef CT_NO_GAME_MUSIC
    ct_music_set_source(NULL);
#else
    ct_music_set_source(&ct_game_music ? &ct_game_music : NULL);
#endif
    ct_text_set_source(ct_game_text);
    ct_pose_set_source(ct_game_pose);
}
