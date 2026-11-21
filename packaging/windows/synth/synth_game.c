/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The game hooks of the synthetic module: all absent. module_entry.c refers
 * to them weakly, which a Windows (PE) link does not allow, so a Windows
 * module defines every one of them (NULL-like values). */
#include "module.h"

int ct_game_scene(ct_scene *out)
{
    (void)out;
    return -1;   /* no scene description */
}

int ct_game_rumble(long frame)
{
    (void)frame;
    return 0;
}

const ct_music_hooks ct_game_music = {0};

int ct_game_text(ct_text *out)
{
    (void)out;
    return -1;
}

int ct_game_pose(int obj, ct_scene_pose *out, uint8_t *rgba, int max)
{
    (void)obj; (void)out; (void)rgba; (void)max;
    return -1;
}
