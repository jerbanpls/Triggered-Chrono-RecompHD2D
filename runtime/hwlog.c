/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Hardware-note log (hwlog.h). */
#include "hwlog.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_NOTES 64
#define NOTE_SIZE 160

static char notes[MAX_NOTES][NOTE_SIZE];
static unsigned n_notes;

void hw_note(const char *fmt, ...)
{
    char text[NOTE_SIZE];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);

    for (unsigned i = 0; i < n_notes; i++)
        if (strcmp(notes[i], text) == 0)
            return;
    if (n_notes < MAX_NOTES)
        memcpy(notes[n_notes++], text, sizeof text);
}

unsigned hw_note_count(void)
{
    return n_notes;
}

const char *hw_note_text(unsigned i)
{
    return i < n_notes ? notes[i] : "";
}

int hw_needed_write(const char *path, const char *tool, const char *stop)
{
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fprintf(f, "# Needed hardware\n\nWritten by `%s --needed-hw`. Do not edit by hand.\n\n", tool);
    if (stop)
        fprintf(f, "Running from reset in the system-mode interpreter stops at:\n\n- %s\n\n", stop);
    else
        fprintf(f, "The run finished without a fatal error.\n\n");
    if (n_notes > 0) {
        fprintf(f, "Stubbed on purpose (documented, first occurrence each):\n\n");
        for (unsigned i = 0; i < n_notes; i++)
            fprintf(f, "- %s\n", notes[i]);
    }
    int bad = ferror(f) != 0;
    if (fclose(f) != 0)
        bad = 1;
    return bad ? -1 : 0;
}
