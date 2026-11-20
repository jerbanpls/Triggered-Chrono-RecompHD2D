/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * WAV writer (wav.h). */
#include "wav.h"

#include <string.h>

#define HEADER_LEN 44
#define CHUNK_FRAMES 512

static void le16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, uint32_t v)
{
    le16(p, v & 0xFFFF);
    le16(p + 2, v >> 16);
}

FILE *wav_open(const char *path, int rate)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return NULL;
    uint8_t h[HEADER_LEN];
    memcpy(h, "RIFF", 4);
    le32(h + 4, 0);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    le16(h + 20, 1);                      /* PCM */
    le16(h + 22, 2);                      /* channels */
    le32(h + 24, (uint32_t)rate);
    le32(h + 28, (uint32_t)rate * 4);     /* bytes per second */
    le16(h + 32, 4);                      /* block align */
    le16(h + 34, 16);                     /* bits per sample */
    memcpy(h + 36, "data", 4);
    le32(h + 40, 0);
    fwrite(h, 1, sizeof h, f);
    return f;
}

void wav_write(FILE *f, const int16_t *stereo, int samples)
{
    uint8_t out[CHUNK_FRAMES * 4];
    while (samples > 0) {
        int n = samples < CHUNK_FRAMES ? samples : CHUNK_FRAMES;
        for (int i = 0; i < n * 2; i++)
            le16(out + i * 2, (uint16_t)stereo[i]);
        fwrite(out, 4, (size_t)n, f);
        stereo += n * 2;
        samples -= n;
    }
}

void wav_close(FILE *f)
{
    if (!f)
        return;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len >= HEADER_LEN) {
            uint8_t b[4];
            le32(b, (uint32_t)(len - 8));
            if (fseek(f, 4, SEEK_SET) == 0)
                fwrite(b, 1, 4, f);
            le32(b, (uint32_t)(len - HEADER_LEN));
            if (fseek(f, 40, SEEK_SET) == 0)
                fwrite(b, 1, 4, f);
        }
    }
    fclose(f);
}
