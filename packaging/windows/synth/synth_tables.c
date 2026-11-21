/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The smallest valid game module (EXPERIMENTAL Windows package test): two
 * "recompiled" functions that no code ever reaches, no externs, no overlays.
 * It contains no game code or data. Built with module_entry.c by
 * tools/build_module.py --synth; the engine reports "2 functions" when it
 * loads it. synth_fn calls an engine function, so the link needs the
 * engine's import library and the load needs the engine program to export
 * it. */
#define CT_TABLES_DEFINE
#include "func_table.h"
#include "sched.h"

static long calls;

static void synth_fn(CPU *cpu)
{
    (void)cpu;
    calls += sched_frame_count();   /* an engine symbol, resolved at load */
}

const ct_func ct_funcs[] = {
    {"synth_a", 0x7E2100, 1, 1, 1, -1, -1, 0, synth_fn},
    {"synth_b", 0x7E2200, 1, 1, 1, -1, -1, 0, synth_fn},
};
const unsigned ct_func_count = 2;
const ct_extern ct_externs[] = {{0, 0, 0}};
const unsigned ct_extern_count = 0;
const ct_jumptable ct_jumptables[] = {{0, 0}};
const unsigned ct_jumptable_count = 0;
const ct_overlay_func ct_overlay_funcs[] = {{0}};
const unsigned ct_overlay_func_count = 0;
