/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PNG writer, WAV writer and hardware-note log (runtime/png, wav, hwlog).
 * No ROM and no game data: every pixel and sample is made up.
 *
 * usage: test_gpl_support TMP_DIR */
#include "harness.h"
#include "hwlog.h"
#include "png.h"
#include "wav.h"

#include <limits.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(d) _mkdir(d)
#else
#define MAKE_DIR(d) mkdir((d), 0777)
#endif

static const char *tmp;

static char *path_of(const char *name)
{
    static char buf[4][1024];
    static int k;
    char *p = buf[k++ & 3];
    snprintf(p, 1024, "%s/%s", tmp, name);
    return p;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc(n > 0 ? (size_t)n : 1);
    if (d && fread(d, 1, (size_t)n, f) != (size_t)n) {
        free(d);
        d = NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return d;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static unsigned le16(const uint8_t *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static uint32_t crc32_of(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return ~c;
}

/* Check a PNG written by png_write_bgrx against the source pixels:
   signature, chunk layout and CRCs, one IDAT, a stored-block zlib stream with
   a correct Adler-32, filter 0 on every row, and every pixel. */
static void check_png(const char *path, const uint8_t *bgrx, int w, int h)
{
    size_t len;
    uint8_t *d = slurp(path, &len);
    CHECK(d != NULL, "read %s", path);
    if (!d)
        return;

    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    CHECK(len > 8 && memcmp(d, sig, 8) == 0, "signature");

    size_t pos = 8, zlen = 0, idats = 0, ihdrs = 0, iends = 0;
    uint8_t *z = malloc(len);
    int order_ok = 1, crc_ok = 1;
    while (pos + 12 <= len) {
        uint32_t n = be32(d + pos);
        if (pos + 12 + (size_t)n > len) {
            order_ok = 0;
            break;
        }
        const uint8_t *type = d + pos + 4;
        if (be32(d + pos + 8 + n) != crc32_of(type, 4 + (size_t)n))
            crc_ok = 0;
        if (!memcmp(type, "IHDR", 4)) {
            if (pos != 8 || n != 13) {
                order_ok = 0;
            } else {
                const uint8_t *b = d + pos + 8;
                CHECK(be32(b) == (uint32_t)w && be32(b + 4) == (uint32_t)h, "IHDR size");
                CHECK(b[8] == 8 && b[9] == 2 && b[10] == 0 && b[11] == 0 && b[12] == 0,
                      "IHDR: 8-bit RGB, deflate, no interlace");
            }
            ihdrs++;
        } else if (!memcmp(type, "IDAT", 4)) {
            memcpy(z + zlen, d + pos + 8, n);
            zlen += n;
            idats++;
        } else if (!memcmp(type, "IEND", 4)) {
            CHECK(n == 0 && pos + 12 == len, "IEND is empty and last");
            iends++;
        }
        pos += 12 + (size_t)n;
    }
    CHECK(order_ok && crc_ok, "chunk lengths and CRCs");
    CHECK(ihdrs == 1 && idats == 1 && iends == 1, "chunks: %zu IHDR, %zu IDAT, %zu IEND",
          ihdrs, idats, iends);

    CHECK(zlen > 6 && (z[0] & 0x0F) == 8 && ((z[0] << 8) | z[1]) % 31 == 0, "zlib header");
    size_t row = (size_t)w * 3 + 1, raw_len = row * (size_t)h;
    uint8_t *raw = malloc(raw_len + 1);
    size_t got = 0, zp = 2;
    int final = 0, stored_ok = 1;
    while (!final && zp + 5 <= zlen) {
        final = z[zp] & 1;
        if ((z[zp] >> 1) != 0) {
            stored_ok = 0;
            break;
        }
        unsigned n = le16(z + zp + 1), nn = le16(z + zp + 3);
        if (n != (~nn & 0xFFFF) || zp + 5 + n > zlen || got + n > raw_len) {
            stored_ok = 0;
            break;
        }
        memcpy(raw + got, z + zp + 5, n);
        got += n;
        zp += 5 + n;
    }
    CHECK(stored_ok && final && got == raw_len && zp + 4 == zlen, "stored blocks cover the data");
    if (stored_ok && got == raw_len && zp + 4 == zlen) {
        uint32_t a = 1, b = 0;
        for (size_t i = 0; i < raw_len; i++) {
            a = (a + raw[i]) % 65521u;
            b = (b + a) % 65521u;
        }
        CHECK(be32(z + zp) == ((b << 16) | a), "Adler-32");

        int px_ok = 1;
        for (int y = 0; y < h && px_ok; y++) {
            const uint8_t *r = raw + (size_t)y * row;
            if (r[0] != 0)
                px_ok = 0;
            for (int x = 0; x < w && px_ok; x++) {
                const uint8_t *s = bgrx + ((size_t)y * w + x) * 4;
                if (r[1 + x * 3] != s[2] || r[2 + x * 3] != s[1] || r[3 + x * 3] != s[0])
                    px_ok = 0;
            }
        }
        CHECK(px_ok, "filter bytes and all %dx%d pixels round-trip", w, h);
    }
    free(raw);
    free(z);
    free(d);
}

static uint8_t *random_frame(int w, int h)
{
    uint8_t *p = malloc((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h * 4; i++)
        p[i] = (uint8_t)rnd32();
    return p;
}

static void test_png_roundtrip(void)
{
    static const int sizes[][2] = { { 1, 1 }, { 7, 5 }, { 256, 224 }, { 200, 150 }, { 3, 40000 } };
    for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++) {
        int w = sizes[k][0], h = sizes[k][1];
        uint8_t *px = random_frame(w, h);
        char *p = path_of("round.png");
        CHECK(png_write_bgrx(p, px, w, h) == 0, "write %dx%d", w, h);
        check_png(p, px, w, h);
        free(px);
    }
    uint8_t one[4] = { 1, 2, 3, 4 };
    CHECK(png_write_bgrx(path_of("no_such_dir/x.png"), one, 1, 1) == -1, "unwritable path");
    CHECK(png_write_bgrx(path_of("zero.png"), one, 0, 1) == -1, "zero width");
}

static void test_png_dump(void)
{
    char *dir = path_of("dump");
    MAKE_DIR(dir);
    enum { W = 8, H = 6 };
    uint8_t black[W * H * 4], a[W * H * 4], b[W * H * 4];
    memset(black, 0, sizeof black);
    for (int i = 0; i < W * H; i++)
        black[i * 4 + 3] = 0xFF;   /* the unused byte does not count */
    memset(a, 0, sizeof a);
    a[(3 * W + 2) * 4 + 1] = 9;
    memcpy(b, a, sizeof b);
    b[(5 * W + 7) * 4] = 1;

    char name[1100];
    CHECK(png_dump_frame(dir, 1, black, W, H) == 0, "black frame skipped");
    snprintf(name, sizeof name, "%s/frame_00001.png", dir);
    CHECK(!exists(name), "no file for a black frame");

    CHECK(png_dump_frame(dir, 7, a, W, H) == 1, "changed frame written");
    snprintf(name, sizeof name, "%s/frame_00007.png", dir);
    CHECK(exists(name), "frame_00007.png exists");
    check_png(name, a, W, H);

    CHECK(png_dump_frame(dir, 8, a, W, H) == 0, "repeat skipped");
    snprintf(name, sizeof name, "%s/frame_00008.png", dir);
    CHECK(!exists(name), "no file for a repeat");

    CHECK(png_dump_frame(dir, 9, black, W, H) == 0, "black between frames skipped");
    CHECK(png_dump_frame(dir, 10, a, W, H) == 0, "black frames do not reset the repeat check");

    CHECK(png_dump_frame(dir, 123456, b, W, H) == 1, "different frame written");
    snprintf(name, sizeof name, "%s/frame_123456.png", dir);
    CHECK(exists(name), "six-digit frame number");

    fprintf(stderr, "(expected next: cannot write ...)\n");
    CHECK(png_dump_frame(path_of("missing_dir"), 11, a, W, H) == -1, "missing directory");
}

static void test_wav(void)
{
    char *p = path_of("t.wav");
    FILE *f = wav_open(p, 32000);
    CHECK(f != NULL, "wav_open");
    int16_t s[6] = { 0x1234, -2, INT16_MIN, INT16_MAX, 0, 1 };
    wav_write(f, s, 3);
    wav_write(f, s + 6 - 2, 0);   /* zero frames: nothing */
    wav_close(f);

    size_t len;
    uint8_t *d = slurp(p, &len);
    CHECK(d && len == 44 + 12, "file length %zu", len);
    if (d && len == 44 + 12) {
        CHECK(!memcmp(d, "RIFF", 4) && le32(d + 4) == len - 8, "RIFF size");
        CHECK(!memcmp(d + 8, "WAVEfmt ", 8) && le32(d + 16) == 16, "WAVE and fmt chunk");
        CHECK(le16(d + 20) == 1 && le16(d + 22) == 2, "PCM, 2 channels");
        CHECK(le32(d + 24) == 32000 && le32(d + 28) == 128000, "rate and byte rate");
        CHECK(le16(d + 32) == 4 && le16(d + 34) == 16, "block align, bits");
        CHECK(!memcmp(d + 36, "data", 4) && le32(d + 40) == 12, "data chunk size");
        static const uint8_t want[12] = { 0x34, 0x12, 0xFE, 0xFF, 0x00, 0x80,
                                          0xFF, 0x7F, 0x00, 0x00, 0x01, 0x00 };
        CHECK(!memcmp(d + 44, want, 12), "little-endian sample bytes");
    }
    free(d);

    f = wav_open(p, 8000);
    wav_close(f);
    d = slurp(p, &len);
    CHECK(d && len == 44 && le32(d + 4) == 36 && le32(d + 40) == 0, "empty file sizes");
    free(d);

    CHECK(wav_open(path_of("no_such_dir/x.wav"), 8000) == NULL, "unwritable path");
}

static void test_notes(void)
{
    CHECK(hw_note_count() == 0, "starts empty");
    CHECK(strcmp(hw_note_text(0), "") == 0, "empty text when none");

    hw_note("first %d", 1);
    hw_note("second");
    hw_note("first %d", 1);
    CHECK(hw_note_count() == 2, "duplicate stored once: %u", hw_note_count());
    hw_note("first %d", 2);
    CHECK(hw_note_count() == 3, "different arguments, different note");
    CHECK(!strcmp(hw_note_text(0), "first 1") && !strcmp(hw_note_text(1), "second")
          && !strcmp(hw_note_text(2), "first 2"), "first-seen order");

    char big[400];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = 0;
    hw_note("%s", big);
    CHECK(strlen(hw_note_text(3)) == 159, "long text cut to 159: %zu", strlen(hw_note_text(3)));
    hw_note("%s", big);
    CHECK(hw_note_count() == 4, "cut text still de-duplicates");

    for (int i = 0; i < 100; i++)
        hw_note("filler %d", i);
    CHECK(hw_note_count() == 64, "cap at 64: %u", hw_note_count());
    CHECK(!strcmp(hw_note_text(4), "filler 0") && !strcmp(hw_note_text(63), "filler 59"),
          "notes past the cap dropped, earlier ones kept");
    CHECK(!strcmp(hw_note_text(64), "") && !strcmp(hw_note_text(UINT_MAX), ""), "out of range");
}

static void test_needed(void)
{
    char *p = path_of("needed.md");
    char want[8192], got[8192];
    char tail[8192] = "";
    size_t t = 0;
    for (unsigned i = 0; i < hw_note_count(); i++)
        t += (size_t)snprintf(tail + t, sizeof tail - t, "- %s\n", hw_note_text(i));

    CHECK(hw_needed_write(p, "tool_x", "boom at $1234") == 0, "write (stop)");
    snprintf(want, sizeof want,
             "# Needed hardware\n\nWritten by `tool_x --needed-hw`. Do not edit by hand.\n\n"
             "Running from reset in the system-mode interpreter stops at:\n\n- boom at $1234\n\n"
             "Stubbed on purpose (documented, first occurrence each):\n\n%s", tail);
    size_t n;
    uint8_t *d = slurp(p, &n);
    snprintf(got, sizeof got, "%.*s", (int)n, d ? (char *)d : "");
    free(d);
    CHECK(!strcmp(got, want), "report with a stop");

    CHECK(hw_needed_write(p, "tool_y", NULL) == 0, "write (no stop)");
    snprintf(want, sizeof want,
             "# Needed hardware\n\nWritten by `tool_y --needed-hw`. Do not edit by hand.\n\n"
             "The run finished without a fatal error.\n\n"
             "Stubbed on purpose (documented, first occurrence each):\n\n%s", tail);
    d = slurp(p, &n);
    snprintf(got, sizeof got, "%.*s", (int)n, d ? (char *)d : "");
    free(d);
    CHECK(!strcmp(got, want), "report without a stop");

    CHECK(hw_needed_write(path_of("no_such_dir/n.md"), "t", NULL) == -1, "unwritable path");
}

static void test_needed_empty(void)
{
    char *p = path_of("needed_empty.md");
    CHECK(hw_note_count() == 0, "no notes yet");
    CHECK(hw_needed_write(p, "tool_z", NULL) == 0, "write (no notes)");
    size_t n;
    uint8_t *d = slurp(p, &n);
    static const char want[] =
        "# Needed hardware\n\nWritten by `tool_z --needed-hw`. Do not edit by hand.\n\n"
        "The run finished without a fatal error.\n\n";
    CHECK(d && n == sizeof want - 1 && !memcmp(d, want, n), "report without notes");
    free(d);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_gpl_support TMP_DIR\n");
        return 2;
    }
    tmp = argv[1];
    MAKE_DIR(tmp);

    test_png_roundtrip();
    test_png_dump();
    test_wav();
    test_needed_empty();
    test_notes();
    test_needed();
    return th_report("gpl_support");
}
