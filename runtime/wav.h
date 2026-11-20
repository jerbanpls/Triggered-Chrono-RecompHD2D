/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * WAV writer: 16-bit stereo PCM. */
#ifndef CT_WAV_H
#define CT_WAV_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create the file and write a 44-byte header for 16-bit stereo PCM at rate
   (sizes patched by wav_close). Returns NULL if the file cannot be created. */
FILE *wav_open(const char *path, int rate);

/* Append `samples` stereo frames (left, right), little-endian. */
void wav_write(FILE *f, const int16_t *stereo, int samples);

/* Patch the RIFF and data sizes into the header, then close. */
void wav_close(FILE *f);

#ifdef __cplusplus
}
#endif

#endif
