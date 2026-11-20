/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PNG writer for screenshots and frame dumps. No compression library:
 * the pixel data is stored in uncompressed deflate blocks. */
#ifndef CT_PNG_H
#define CT_PNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Write w*h pixels (4 bytes each: blue, green, red, unused; rows top first,
   no padding) as an 8-bit RGB PNG. Returns 0 on success, -1 on any failure. */
int png_write_bgrx(const char *path, const uint8_t *bgrx, int w, int h);

/* Write <dir>/frame_NNNNN.png unless the frame is all black or identical to
   the last frame this function wrote. Returns 1 written, 0 skipped, -1 error. */
int png_dump_frame(const char *dir, long frame, const uint8_t *bgrx, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
