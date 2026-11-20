/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Log of hardware behaviour that is stubbed on purpose: each distinct
 * message is kept once, in the order first seen. */
#ifndef CT_HWLOG_H
#define CT_HWLOG_H

#include "cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* printf-style; text is cut to 159 characters; duplicates are ignored;
   at most 64 distinct messages are kept. */
void hw_note(const char *fmt, ...) CT_PRINTF(1, 2);

unsigned hw_note_count(void);

/* Message i in first-seen order, or "" when i is out of range. */
const char *hw_note_text(unsigned i);

/* Markdown report of the notes. `stop` is the fatal stop reason or NULL.
   Returns 0 on success, -1 if the file cannot be created or closed. */
int hw_needed_write(const char *path, const char *tool, const char *stop);

#ifdef __cplusplus
}
#endif

#endif
