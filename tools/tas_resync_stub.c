/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: ct_boot --resync (tas_resync.h) on Windows: not available. The real
 * one (tas_resync.c) keeps its checkpoints as forked processes and maps the
 * video with mmap, so it is Linux only. */
#include <stdio.h>

#include "tas_resync.h"

int resync_init(const char *ref, long lag, const char *out, int jobs)
{
    (void)ref;
    (void)lag;
    (void)out;
    (void)jobs;
    fprintf(stderr, "ct_boot: --resync needs fork and mmap: Linux only\n");
    return -1;
}

int resync_active(void) { return 0; }

uint16_t resync_buttons(const replay *r, long f)
{
    (void)r;
    (void)f;
    return 0;
}

void resync_frame(long f, const uint8_t *bgrx)
{
    (void)f;
    (void)bgrx;
}

void resync_fatal(long f) { (void)f; }
void resync_finish(long f) { (void)f; }
