/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PNG writer (png.h). The whole file is assembled in memory and written
 * with a single fwrite. */
#include "png.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STORED_MAX 65535u

static uint32_t crc_table[256];
static int crc_ready;

static void crc_init(void)
{
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
    crc_ready = 1;
}

static uint32_t crc_update(uint32_t crc, const uint8_t *p, size_t n)
{
    if (!crc_ready)
        crc_init();
    for (size_t i = 0; i < n; i++)
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* Fill in the length and CRC around a chunk whose type and data already sit
   at c+4. Returns the total chunk size. */
static size_t seal_chunk(uint8_t *c, uint32_t len)
{
    put_be32(c, len);
    put_be32(c + 8 + len, crc_update(0xFFFFFFFFu, c + 4, 4 + (size_t)len) ^ 0xFFFFFFFFu);
    return 12 + (size_t)len;
}

static uint32_t adler32(const uint8_t *p, size_t n)
{
    uint32_t a = 1, b = 0;
    while (n > 0) {
        size_t run = n < 5552 ? n : 5552;
        n -= run;
        while (run--) {
            a += *p++;
            b += a;
        }
        a %= 65521u;
        b %= 65521u;
    }
    return (b << 16) | a;
}

int png_write_bgrx(const char *path, const uint8_t *bgrx, int w, int h)
{
    if (!path || !bgrx || w <= 0 || h <= 0)
        return -1;

    size_t row = (size_t)w * 3 + 1;
    if ((size_t)h > (SIZE_MAX / 4) / row)
        return -1;
    size_t raw_len = row * (size_t)h;
    size_t blocks = (raw_len + STORED_MAX - 1) / STORED_MAX;
    size_t zlen = 2 + raw_len + blocks * 5 + 4;
    if (zlen > 0x7FFFFFFFu)
        return -1;

    size_t total = 8 + (12 + 13) + (12 + zlen) + 12;
    uint8_t *buf = malloc(total + raw_len);
    if (!buf)
        return -1;
    uint8_t *raw = buf + total;

    /* Scanlines: a zero filter byte, then red, green, blue per pixel. */
    uint8_t *r = raw;
    for (int y = 0; y < h; y++) {
        const uint8_t *s = bgrx + (size_t)y * (size_t)w * 4;
        *r++ = 0;
        for (int x = 0; x < w; x++, s += 4) {
            *r++ = s[2];
            *r++ = s[1];
            *r++ = s[0];
        }
    }

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    memcpy(buf, sig, 8);
    uint8_t *p = buf + 8;

    memcpy(p + 4, "IHDR", 4);
    put_be32(p + 8, (uint32_t)w);
    put_be32(p + 12, (uint32_t)h);
    p[16] = 8;   /* bit depth */
    p[17] = 2;   /* colour type: RGB */
    p[18] = 0;   /* compression: deflate */
    p[19] = 0;   /* filter method */
    p[20] = 0;   /* no interlace */
    p += seal_chunk(p, 13);

    memcpy(p + 4, "IDAT", 4);
    uint8_t *q = p + 8;
    *q++ = 0x78;
    *q++ = 0x01;
    for (size_t off = 0; off < raw_len;) {
        size_t n = raw_len - off < STORED_MAX ? raw_len - off : STORED_MAX;
        *q++ = (off + n == raw_len);   /* BFINAL, BTYPE 00 (stored) */
        *q++ = (uint8_t)n;
        *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n;
        *q++ = (uint8_t)(~n >> 8);
        memcpy(q, raw + off, n);
        q += n;
        off += n;
    }
    put_be32(q, adler32(raw, raw_len));
    q += 4;
    p += seal_chunk(p, (uint32_t)(q - (p + 8)));

    memcpy(p + 4, "IEND", 4);
    p += seal_chunk(p, 0);

    int rc = 0;
    FILE *f = fopen(path, "wb");
    if (!f) {
        rc = -1;
    } else {
        if (fwrite(buf, 1, (size_t)(p - buf), f) != (size_t)(p - buf))
            rc = -1;
        if (fclose(f) != 0)
            rc = -1;
    }
    free(buf);
    return rc;
}

static int have_last;
static uint64_t last_print;

int png_dump_frame(const char *dir, long frame, const uint8_t *bgrx, int w, int h)
{
    if (!dir || !bgrx || w <= 0 || h <= 0)
        return -1;

    uint64_t print = 0xCBF29CE484222325ull ^ ((uint64_t)(unsigned)w << 32) ^ (uint64_t)(unsigned)h;
    int lit = 0;
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *s = bgrx + i * 4;
        if (s[0] | s[1] | s[2])
            lit = 1;
        for (int k = 0; k < 3; k++) {
            print ^= s[k];
            print *= 0x100000001B3ull;
        }
    }
    if (!lit || (have_last && print == last_print))
        return 0;

    char path[1024];
    if (snprintf(path, sizeof path, "%s/frame_%05ld.png", dir, frame) >= (int)sizeof path
        || png_write_bgrx(path, bgrx, w, h) != 0) {
        fprintf(stderr, "cannot write %s\n", path);
        return -1;
    }
    have_last = 1;
    last_print = print;
    return 1;
}
