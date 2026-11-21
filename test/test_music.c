/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: audio packs (frontend/packs/music.h) with synthetic tones this
 * test writes itself into a pack under DIR: no game music anywhere.
 * Songs start, loop, stop and follow fades; an unmapped song and no pack
 * leave the output exactly as it was.
 *
 * usage: test_music DIR [FILE.ogg|FILE.flac ...]
 * Each extra FILE (made by test_music_codecs.py with ffmpeg, a 440 Hz
 * tone, in DIR/testpack/, named relative to it) is played through its
 * decoder and checked the same way. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "harness.h"
#include "music.h"
#include "wav.h"
#include "platform.h"   /* port: Windows: mkdir with no mode, setenv */

#define RATE 48000
#define PI 3.14159265358979323846

static int16_t buf[RATE * 4 * 2];

/* A 440 Hz tone of amplitude amp, `seconds` long, at `rate`. */
static void write_tone(const char *path, int rate, double seconds, double amp)
{
    FILE *f = wav_open(path, rate);
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(2);
    }
    int n = (int)(rate * seconds);
    for (int k = 0; k < n; k++) {
        int16_t v = (int16_t)lrint(amp * sin(2 * PI * 440.0 * k / rate));
        int16_t s[2] = {v, v};
        wav_write(f, s, 1);
    }
    wav_close(f);
}

/* RMS of the left channel over frames [a, b). */
static double rms(const int16_t *s, int a, int b)
{
    double sum = 0;
    for (int k = a; k < b; k++)
        sum += (double)s[2 * k] * s[2 * k];
    return b > a ? sqrt(sum / (b - a)) : 0;
}

/* Upward zero crossings per second over [a, b): the tone's frequency. */
static double freq(const int16_t *s, int a, int b)
{
    int c = 0;
    for (int k = a + 1; k < b; k++)
        if (s[2 * (k - 1)] < 0 && s[2 * k] >= 0)
            c++;
    return c * (double)RATE / (b - a);
}

static void render(int frames)
{
    memset(buf, 0, (size_t)frames * 4);
    music_mix(buf, frames);
}

static ct_music_event ev(int kind, int song, int volume, int frames)
{
    return (ct_music_event){.kind = (uint8_t)kind, .song = (uint8_t)song,
                            .volume = (uint8_t)volume, .frames = (uint16_t)frames};
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: test_music DIR [FILE ...]\n");
        return 2;
    }
    char dir[4096], path[4200];
    snprintf(dir, sizeof dir, "%s/testpack", argv[1]);
    mkdir(argv[1], 0755);
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/music", dir);
    mkdir(path, 0755);
    /* At 32 kHz, so the stream is resampled to 48 kHz. Amplitude 8000:
       RMS 5657. */
    snprintf(path, sizeof path, "%s/music/loop.wav", dir);
    write_tone(path, 32000, 0.5, 8000);
    snprintf(path, sizeof path, "%s/music/once.wav", dir);
    write_tone(path, 48000, 0.25, 8000);
    snprintf(path, sizeof path, "%s/pack.toml", dir);
    FILE *f = fopen(path, "w");
    fprintf(f, "# A test pack of synthetic tones (test_music.c).\n"
               "[pack]\nversion = 1\nname = \"testpack\"\n\n"
               "[[music]]\nsong = 0x18\nfile = \"music/loop.wav\"\nloop_start = 8000\n\n"
               "[[music]]\nsong = 0x19\nfile = \"music/once.wav\"\nloop = false\n\n"
               "[[music]]\nsong = 0x1A\nfile = \"music/loop.wav\"\nfade = \"none\"\n"
               "volume = 0.5\n\n[[tile]]\nkey = \"0000000000000001\"\n");
    for (int k = 2; k < argc; k++)
        fprintf(f, "\n[[music]]\nsong = %d\nfile = \"%s\"\n", 0x40 + k, argv[k]);
    fclose(f);

    /* No pack loaded: nothing is mixed in, byte for byte. */
    music_set_rate(RATE);
    for (int k = 0; k < 4800; k++)
        buf[k] = (int16_t)(k * 7);
    music_mix(buf, 2400);
    int same = 1;
    for (int k = 0; k < 4800; k++)
        same &= buf[k] == (int16_t)(k * 7);
    CHECK(same && !music_replacing(), "no pack: the output is untouched");

    const char *dirs[1] = {dir};
    int n = music_load(dirs, 1);
    CHECK(n == 3 + argc - 2, "pack.toml: %d songs", n);
    CHECK(music_file(0x18) && strstr(music_file(0x18), "music/loop.wav"), "song 0x18's file");
    CHECK(!music_file(0x30), "song 0x30 is not in the pack");

    /* An unmapped song changes nothing. */
    ct_music_event e = ev(CT_MUSIC_PLAY, 0x30, 0, 0);
    music_event(&e, 1);
    render(4800);
    CHECK(!music_replacing() && rms(buf, 0, 4800) == 0, "unmapped song: silent, not replacing");

    /* Song 0x18 starts: the tone at full height, 440 Hz. */
    e = ev(CT_MUSIC_PLAY, 0x18, 0, 0);
    music_event(&e, 2);
    CHECK(music_replacing(), "song 0x18 replaces the game's");
    render(RATE * 2);   /* 2 s: the 0.5 s file loops from 0.25 s, several times */
    double r0 = rms(buf, 2400, 24000), r1 = rms(buf, RATE * 2 - 24000, RATE * 2);
    CHECK(fabs(r0 - 5657) < 300, "playing: RMS %.0f (5657 expected)", r0);
    CHECK(fabs(freq(buf, 2400, RATE) - 440) < 5, "playing: %.1f Hz", freq(buf, 2400, RATE));
    CHECK(fabs(r1 - 5657) < 300, "looped: still playing after 2 s, RMS %.0f", r1);
    /* At the loop point (from 0.5 s back to 0.25 s, a whole number of
       periods: 110 of them) the tone runs on without a jump. */
    int worst = 0;
    for (int k = 1; k < RATE * 2; k++) {
        int d = abs(buf[2 * k] - buf[2 * (k - 1)]);
        worst = d > worst ? d : worst;
    }
    CHECK(worst < 600, "no click at the loop: largest step %d", worst);

    /* The same song again (a room with the same music): it plays on, no
       declick dip. */
    e = ev(CT_MUSIC_PLAY, 0x18, 0, 0);
    music_event(&e, 3);
    render(960);
    CHECK(rms(buf, 0, 960) > 5000, "the same song again plays on: RMS %.0f", rms(buf, 0, 960));

    /* The game fades to 0 over 30 frames (0.499 s): half way, about half;
       after it, silent. */
    e = ev(CT_MUSIC_FADE, 0, 0, 30);
    music_event(&e, 4);
    render(RATE);
    double mid = rms(buf, 11000, 13000), after = rms(buf, 25000, RATE);
    CHECK(fabs(mid - 5657 / 2.0) < 400, "fade half way: RMS %.0f", mid);
    CHECK(after < 1, "faded out: RMS %.1f", after);
    CHECK(music_replacing(), "a fade to 0 still replaces (the song may come back)");
    /* ... and back up at once. */
    e = ev(CT_MUSIC_FADE, 0, 255, 0);
    music_event(&e, 5);
    render(4800);
    CHECK(fabs(rms(buf, 0, 4800) - 5657) < 300, "volume back: RMS %.0f", rms(buf, 0, 4800));

    /* Stop: silent within the declick, and the game's music back. */
    e = ev(CT_MUSIC_STOP, 0, 0, 0);
    music_event(&e, 6);
    CHECK(!music_replacing(), "stop: not replacing");
    render(4800);
    CHECK(rms(buf, 1200, 4800) == 0, "stopped: silent after 20 ms");

    /* A song that doesn't loop ends by itself. */
    e = ev(CT_MUSIC_PLAY, 0x19, 0, 0);
    music_event(&e, 7);
    render(RATE / 2);
    CHECK(rms(buf, 1200, 9600) > 5000 && rms(buf, 13000, RATE / 2) == 0,
          "loop = false: plays 0.25 s, then silence (%.0f, %.0f)", rms(buf, 1200, 9600),
          rms(buf, 13000, RATE / 2));

    /* fade = "none": the game's fades are ignored; volume = 0.5. */
    e = ev(CT_MUSIC_PLAY, 0x1A, 0, 0);
    music_event(&e, 8);
    e = ev(CT_MUSIC_FADE, 0, 0, 0);
    music_event(&e, 9);
    render(9600);
    CHECK(fabs(rms(buf, 2400, 9600) - 5657 / 2.0) < 300, "fade = none, volume 0.5: RMS %.0f",
          rms(buf, 2400, 9600));
    e = ev(CT_MUSIC_FADE, 0, 255, 0);
    music_event(&e, 10);

    /* The decoders: each extra file, a 440 Hz tone. */
    for (int k = 2; k < argc; k++) {
        e = ev(CT_MUSIC_PLAY, 0x40 + k, 0, 0);
        music_event(&e, 11 + k);
        CHECK(music_replacing(), "%s opens", argv[k]);
        render(RATE / 2);
        double r = rms(buf, 2400, RATE / 2), fq = freq(buf, 2400, RATE / 2);
        CHECK(r > 2000 && fabs(fq - 440) < 5, "%s: RMS %.0f at %.1f Hz", argv[k], r, fq);
    }

    music_unload();
    CHECK(!music_replacing() && music_count() == 0, "unloaded");
    return th_report("music");
}
