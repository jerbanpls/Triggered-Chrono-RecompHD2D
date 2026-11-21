/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Asset packs (frontend/packs): PNG reading and writing, content keys,
 * the dump, loading with priorities, the atlas and the per-pixel map,
 * and [[model]] entries with a glTF cube made for the test, [[pose]]
 * entries and their pixels in the map. No game data:
 * every picture and VRAM byte here is made up.
 *
 * usage: test_packs SOURCE_DIR TMP_DIR */
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

/* The shell commands below are sh's (mkdir -p, rm -rf); Windows' system()
   runs cmd.exe, so there they go through sh (MSYS2's, on PATH). */
#ifdef _WIN32
static int run(const char *cmd)
{
    char b[4800];
    snprintf(b, sizeof b, "sh -c \"%s\"", cmd);
    return system(b);
}
#else
#define run(cmd) system(cmd)
#endif

#include "music.h"
#include "packs.h"
#include "pngio.h"

/* Generated test vectors: made-up pictures, compressed by zlib. */
static const uint8_t png_rgb[218] = {
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
    0, 0, 0, 9, 0, 0, 0, 6, 8, 2, 0, 0, 0, 158, 165, 35,
    146, 0, 0, 0, 161, 73, 68, 65, 84, 120, 218, 99, 96, 96, 96, 87,
    101, 100, 247, 98, 97, 207, 231, 100, 159, 34, 192, 190, 83, 146, 253, 158,
    10, 59, 179, 33, 187, 134, 3, 59, 35, 55, 51, 80, 78, 86, 149, 89,
    86, 149, 85, 86, 149, 93, 86, 149, 83, 86, 149, 91, 86, 149, 87, 86,
    149, 95, 150, 137, 155, 153, 129, 155, 89, 150, 155, 217, 138, 155, 57, 156,
    155, 185, 132, 155, 121, 34, 55, 243, 58, 110, 230, 211, 220, 204, 47, 152,
    197, 216, 88, 36, 152, 172, 36, 152, 61, 37, 88, 194, 37, 88, 159, 73,
    176, 149, 72, 176, 55, 207, 224, 16, 148, 224, 84, 96, 1, 233, 99, 68,
    214, 7, 68, 235, 84, 65, 164, 61, 131, 57, 63, 123, 140, 192, 140, 70,
    97, 205, 101, 18, 187, 78, 203, 123, 127, 208, 184, 35, 106, 156, 107, 229,
    240, 47, 222, 191, 31, 0, 145, 65, 29, 91, 179, 18, 9, 239, 0, 0,
    0, 0, 73, 69, 78, 68, 174, 66, 96, 130,
};
static const uint8_t png_rgb_want[216] = {
    0, 0, 7, 255, 37, 1, 7, 255, 74, 4, 7, 255, 111, 9, 7, 255,
    148, 16, 7, 255, 185, 25, 7, 255, 222, 36, 7, 255, 3, 49, 7, 255,
    40, 64, 7, 255, 11, 3, 7, 255, 48, 4, 36, 255, 85, 7, 65, 255,
    122, 12, 94, 255, 159, 19, 123, 255, 196, 28, 152, 255, 233, 39, 181, 255,
    14, 52, 210, 255, 51, 67, 239, 255, 22, 6, 7, 255, 59, 7, 65, 255,
    96, 10, 123, 255, 133, 15, 181, 255, 170, 22, 239, 255, 207, 31, 41, 255,
    244, 42, 99, 255, 25, 55, 157, 255, 62, 70, 215, 255, 33, 9, 7, 255,
    70, 10, 94, 255, 107, 13, 181, 255, 144, 18, 12, 255, 181, 25, 99, 255,
    218, 34, 186, 255, 255, 45, 17, 255, 36, 58, 104, 255, 73, 73, 191, 255,
    44, 12, 7, 255, 81, 13, 123, 255, 118, 16, 239, 255, 155, 21, 99, 255,
    192, 28, 215, 255, 229, 37, 75, 255, 10, 48, 191, 255, 47, 61, 51, 255,
    84, 76, 167, 255, 55, 15, 7, 255, 92, 16, 152, 255, 129, 19, 41, 255,
    166, 24, 186, 255, 203, 31, 75, 255, 240, 40, 220, 255, 21, 51, 109, 255,
    58, 64, 254, 255, 95, 79, 143, 255,
};
static const uint8_t png_pal[150] = {
    137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
    0, 0, 0, 5, 0, 0, 0, 3, 4, 3, 0, 0, 0, 169, 24, 216,
    203, 0, 0, 0, 48, 80, 76, 84, 69, 0, 255, 0, 10, 245, 3, 20,
    235, 6, 30, 225, 9, 40, 215, 12, 50, 205, 15, 60, 195, 18, 70, 185,
    21, 80, 175, 24, 90, 165, 27, 100, 155, 30, 110, 145, 33, 120, 135, 36,
    130, 125, 39, 140, 115, 42, 150, 105, 45, 176, 130, 53, 132, 0, 0, 0,
    1, 116, 82, 78, 83, 0, 64, 230, 216, 102, 0, 0, 0, 20, 73, 68,
    65, 84, 120, 218, 99, 96, 84, 118, 96, 80, 118, 77, 96, 112, 77, 111,
    0, 0, 10, 69, 2, 89, 138, 187, 92, 175, 0, 0, 0, 0, 73, 69,
    78, 68, 174, 66, 96, 130,
};
static const uint8_t png_pal_want[60] = {
    0, 255, 0, 0, 10, 245, 3, 255, 20, 235, 6, 255, 30, 225, 9, 255,
    40, 215, 12, 255, 20, 235, 6, 255, 30, 225, 9, 255, 40, 215, 12, 255,
    50, 205, 15, 255, 60, 195, 18, 255, 40, 215, 12, 255, 50, 205, 15, 255,
    60, 195, 18, 255, 70, 185, 21, 255, 80, 175, 24, 255,
};


static long checks, fails;
#define CHECK(cond, ...)                                                              \
    do {                                                                              \
        checks++;                                                                     \
        if (!(cond) && fails++ < 30) {                                                \
            fprintf(stderr, "%s:%d: FAIL %s: ", __FILE__, __LINE__, #cond);           \
            fprintf(stderr, __VA_ARGS__);                                             \
            fputc('\n', stderr);                                                      \
        }                                                                             \
    } while (0)

static void test_png(const char *tmp)
{
    int w, h;
    const char *err = NULL;
    uint8_t *img = pngio_decode_rgba(png_rgb, sizeof png_rgb, &w, &h, &err);
    CHECK(img && w == 9 && h == 6, "RGB PNG (all filters, dynamic Huffman): %s", err ? err : "");
    CHECK(img && !memcmp(img, png_rgb_want, sizeof png_rgb_want), "RGB PNG pixels");
    free(img);
    img = pngio_decode_rgba(png_pal, sizeof png_pal, &w, &h, &err);
    CHECK(img && w == 5 && h == 3, "4-bit palette PNG: %s", err ? err : "");
    CHECK(img && !memcmp(img, png_pal_want, sizeof png_pal_want), "palette PNG pixels and tRNS");
    free(img);
    /* Our writer, read back. */
    uint8_t px[13 * 7 * 4];
    for (size_t k = 0; k < sizeof px; k++)
        px[k] = (uint8_t)(k * 7 + k / 13);
    char path[4096];
    snprintf(path, sizeof path, "%s/rt.png", tmp);
    CHECK(!pngio_write_rgba(path, px, 13, 7), "write %s", path);
    img = pngio_read_rgba(path, &w, &h, &err);
    CHECK(img && w == 13 && h == 7 && !memcmp(img, px, sizeof px), "RGBA round trip");
    free(img);
    /* Damaged data fails cleanly. */
    uint8_t bad[sizeof png_rgb];
    memcpy(bad, png_rgb, sizeof bad);
    for (size_t k = 45; k < 60; k++)
        bad[k] ^= 0x5A;
    img = pngio_decode_rgba(bad, sizeof bad, &w, &h, &err);
    CHECK(!img, "a damaged PNG is refused");
    free(img);
}

/* A made-up VRAM/CGRAM: a 4bpp character at $1000 (BG), a 16x16 sprite
   (names 2, 3, 18, 19 under the table at $4000). */
static sched_vmem mem;

static void fill_mem(void)
{
    memset(&mem, 0, sizeof mem);
    for (int r = 0; r < 8; r++) {
        mem.vram[0x1000 + r] = (uint16_t)(0x80 >> r | 0x3C00);
        mem.vram[0x1000 + 8 + r] = (uint16_t)(0x0F << (r & 3));
    }
    for (int c = 0; c < 4; c++)
        for (int r = 0; r < 8; r++) {
            int chr = (c & 1) + (c >> 1) * 16 + 2;
            mem.vram[0x4000 + chr * 16 + r] = (uint16_t)((0xF0 >> c) | (r << 9));
            mem.vram[0x4000 + chr * 16 + 8 + r] = (uint16_t)(0x11 << (r & 3));
        }
    for (int k = 0; k < 256; k++)
        mem.cgram[k] = (uint16_t)((k * 37) & 0x7fff);
}

static sched_prov_px bg_px(int u, int v, int flags)
{
    sched_prov_px p;
    memset(&p, 0, sizeof p);
    p.chr = 0x1000;
    p.pal = 16;
    p.flags = (uint8_t)(2 | flags);
    p.u = (uint8_t)u;
    p.v = (uint8_t)v;
    p.size = 8;
    p.cidx = 17;
    p.layer = 0x80;
    return p;
}

static sched_prov_px obj_px(int u, int v)
{
    sched_prov_px p;
    memset(&p, 0, sizeof p);
    p.chr = 0x4000;
    p.name = 2;
    p.size = 16;
    p.pal = 0x90;
    p.flags = 2 | SCHED_PROV_OBJ;
    p.u = (uint8_t)u;
    p.v = (uint8_t)v;
    p.cidx = 0x91;
    p.layer = 0xa4;
    return p;
}

static void test_keys(void)
{
    fill_mem();
    sched_prov_px a = bg_px(0, 0, 0), b = bg_px(5, 3, SCHED_PROV_HFLIP | SCHED_PROV_VFLIP);
    uint64_t ka = packs_key(&mem, &a);
    CHECK(ka != 0, "BG key");
    CHECK(packs_key_tile(mem.vram, mem.cgram, 0x1000, 16, 4) == ka, "packs_key_tile: the same key");
    CHECK(packs_key(&mem, &b) == ka, "flips and texel are not part of the key");
    mem.cgram[0] ^= 0x1234;
    mem.cgram[16] ^= 0x1234;   /* the palette's color 0: transparent */
    CHECK(packs_key(&mem, &a) == ka, "color 0 is not part of the key");
    mem.cgram[17] ^= 1;
    CHECK(packs_key(&mem, &a) != ka, "a palette color is");
    fill_mem();
    a.pal = 32;
    CHECK(packs_key(&mem, &a) != ka, "another palette, another key");
    a.pal = 16;
    mem.vram[0x1003] ^= 0x0100;
    CHECK(packs_key(&mem, &a) != ka, "another pixel, another key");
    fill_mem();
    sched_prov_px none = a;
    none.flags = 0;
    CHECK(packs_key(&mem, &none) == 0, "nothing to replace: key 0");

    uint8_t rgba[64 * 64 * 4];
    int w, h;
    sched_prov_px s = obj_px(0, 0);
    CHECK(!packs_decode(&mem, &s, rgba, &w, &h) && w == 16 && h == 16, "sprite decode %dx%d", w, h);
    /* Pixel (8, 8) is character 19's (0, 0): row 0 bit 7 of its planes. */
    int chr = 19, v = ((mem.vram[0x4000 + chr * 16] >> 7) & 1) | ((mem.vram[0x4000 + chr * 16] >> 15) & 1) << 1 |
                     ((mem.vram[0x4000 + chr * 16 + 8] >> 7) & 1) << 2 | ((mem.vram[0x4000 + chr * 16 + 8] >> 15) & 1) << 3;
    uint16_t c = mem.cgram[0x90 + v];
    CHECK(rgba[(8 * 16 + 8) * 4 + 3] == (v ? 255 : 0) && (!v || rgba[(8 * 16 + 8) * 4] >> 3 == (c & 31)),
          "sprite pixel (8, 8) from character 19 (16x16 page wrap)");
    uint64_t ks = packs_key(&mem, &s);
    s.name = 3;
    CHECK(packs_key(&mem, &s) != ks, "another name, another sprite key");
    char t[17];
    packs_key_text(0x0123456789abcdefull, t);
    CHECK(!strcmp(t, "0123456789abcdef"), "key text %s", t);
}

static int file_exists(const char *p)
{
    FILE *f = fopen(p, "rb");
    if (f)
        fclose(f);
    return f != NULL;
}

static int count_in(const char *path, const char *needle)
{
    FILE *f = fopen(path, "r");
    char line[1024];
    int n = 0;
    while (f && fgets(line, sizeof line, f))
        n += strstr(line, needle) != NULL;
    if (f)
        fclose(f);
    return n;
}

static sched_prov_px frame[SCHED_WIDTH * SCHED_HEIGHT];
static uint16_t map[SCHED_WIDTH * SCHED_HEIGHT * 4];

static void test_dump_and_load(const char *tmp)
{
    fill_mem();
    /* A frame: BG tile over the top-left 8x8, two sprites drawn together
       (OAM 0 at 40,0 and OAM 1 at 56,0), each 16x16. */
    memset(frame, 0, sizeof frame);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            frame[y * SCHED_WIDTH + x] = bg_px(x, y, 0);
    for (int y = 0; y < 16; y++)
        for (int x = 0; x < 16; x++) {
            sched_prov_px p = obj_px(x, y);
            frame[y * SCHED_WIDTH + 40 + x] = p;
            p.index = 1;
            p.name = 4;
            frame[y * SCHED_WIDTH + 56 + x] = p;
        }
    mem.oam[0] = 40;
    mem.oam[1] = 2;
    mem.oam[2] = 56;
    mem.oam[3] = 4;
    mem.oam[0x100] = 0x0A;   /* entries 0 and 1 large */
    char dir[4096], path[4096];
    snprintf(dir, sizeof dir, "%s/packs/dumped", tmp);
    CHECK(!packs_dump_open(dir, "dumped"), "dump open");
    packs_dump_frame(1, frame, &mem);
    packs_dump_frame(2, frame, &mem);   /* nothing new */
    long pieces, objects;
    packs_dump_counts(&pieces, &objects);
    CHECK(pieces == 3 && objects == 1, "dumped %ld pieces, %ld objects (want 3, 1)", pieces, objects);
    packs_dump_close();
    snprintf(path, sizeof path, "%s/pack.toml", dir);
    CHECK(count_in(path, "[[tile]]") == 3 && count_in(path, "[[object]]") == 1, "manifest entries");
    CHECK(count_in(path, "first_frame = 1") == 4, "first frames");
    CHECK(count_in(path, "layer = \"OBJ\"") == 2 && count_in(path, "layer = \"BG1\"") == 1, "layer tags");
    uint64_t kbg = packs_key(&mem, &frame[0]);
    char kt[17];
    packs_key_text(kbg, kt);
    snprintf(path, sizeof path, "%s/tiles/%s.png", dir, kt);
    CHECK(file_exists(path), "tile PNG %s", path);
    /* Opened again: what is there is not written twice. */
    CHECK(!packs_dump_open(dir, "dumped"), "dump reopen");
    packs_dump_frame(3, frame, &mem);
    packs_dump_counts(&pieces, &objects);
    CHECK(pieces == 0 && objects == 0, "reopened: nothing new (%ld, %ld)", pieces, objects);
    packs_dump_close();

    /* Replace the BG tile with a 32x32 picture in a second pack that has
       priority over the dump. */
    char dir2[4096];
    snprintf(dir2, sizeof dir2, "%s/packs/hi", tmp);
    snprintf(path, sizeof path, "mkdir -p '%s/tiles'", dir2);
    CHECK(run(path) == 0, "mkdir");
    static uint8_t big[32 * 32 * 4];
    memset(big, 0x7F, sizeof big);
    snprintf(path, sizeof path, "%s/tiles/x.png", dir2);
    CHECK(!pngio_write_rgba(path, big, 32, 32), "write 32x32");
    snprintf(path, sizeof path, "%s/pack.toml", dir2);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "[pack]\nversion = 1\nscale = 4\n\n[[tile]]\nkey = \"%s\"\nfile = \"tiles/x.png\"\nw = 8\nh = 8\n", kt);
        fclose(f);
    }
    const char *dirs[2] = {dir2, dir};
    int n = packs_load(dirs, 2);
    CHECK(n == 3, "loaded %d replacements (want 3: the higher pack wins one key)", n);
    const packs_rect *r = packs_lookup_tile(kbg);
    CHECK(r && r->w == 32 && r->h == 32 && r->src_w == 8 && r->src_h == 8, "lookup: the 32x32 one");
    int size, pages;
    const uint8_t *atlas = packs_atlas(&size, &pages);
    CHECK(atlas && pages == 1 && r && atlas[((size_t)r->y * size + r->x) * 4] == 0x7F, "atlas holds it");
    CHECK(packs_lookup_tile(12345) == NULL, "unknown key");
    long replaced = packs_map_frame(frame, &mem, map);
    CHECK(replaced == 64 + 2 * 256, "map: %ld pixels replaced", replaced);
    int e = map[0] - 1;
    int ne;
    const packs_rect *all = packs_entries(&ne);
    CHECK(e >= 0 && e < ne && all[e].key == kbg, "map entry for pixel 0");
    const uint16_t *m = &map[(3 * SCHED_WIDTH + 5) * 4];
    CHECK(m[1] == (5 | 3 << 8) && m[3] == (mem.cgram[17] & 0x7fff), "map texel and color");
    CHECK(map[(100 * SCHED_WIDTH + 100) * 4] == 0, "no replacement where nothing was drawn");
    unsigned gen = packs_generation();
    CHECK(packs_reload() == 3 && packs_generation() != gen, "reload");
    packs_unload();
    CHECK(!packs_active() && packs_lookup_tile(kbg) == NULL, "unload");
}

static void test_models(const char *src)
{
    char dir[4096], err[512];
    snprintf(dir, sizeof dir, "%s/test/data/packs/models", src);
    const char *dirs[1] = {dir};
    CHECK(packs_load(dirs, 1) == 2, "model pack: no tiles, two models");
    int n;
    const packs_model *m = packs_models(&n);
    CHECK(n == 2, "%d [[model]] entries", n);
    if (n < 2)
        return;
    CHECK(m[0].map == 0x1F0 && m[0].cell_x == 3 && m[0].cell_y == 4 && m[0].scale[0] == 16.0f &&
              m[0].scale[2] == 16.0f && m[0].offset[0] == 8.0f && m[0].offset[2] == 8.0f &&
              m[0].cells_w == 1 && !m[0].n_tiles && !m[0].hide_cells && m[0].color[3] == 1.0f,
          "entry: map %x cell %d,%d scale %g", m[0].map, m[0].cell_x, m[0].cell_y, m[0].scale[0]);
    packs_model pm = m[0];
    CHECK(!packs_model_parse(pm.file, &pm, err, sizeof err), "parse the cube: %s", err);
    CHECK(pm.meshes == 1 && pm.primitives == 1, "cube: %d meshes, %d primitives", pm.meshes, pm.primitives);
    CHECK(pm.min[0] == -0.5f && pm.max[1] == 0.5f && pm.max[2] == 0.5f, "cube bounds");
    CHECK(m[0].meshes == 1, "loading parsed it too");
    pm = m[1];
    CHECK(packs_model_parse(pm.file, &pm, err, sizeof err) && strstr(err, "not found"), "missing file: %s", err);
    CHECK(m[1].meshes == 0, "a model that failed has no meshes");
    /* The triangles: the cube's 12, its normals unit length and facing out. */
    const packs_mesh *mesh = packs_model_mesh(0);
    CHECK(mesh && mesh->ni == 36 && mesh->nparts == 1 && mesh->part[0].count == 36,
          "cube mesh: %d indices", mesh ? mesh->ni : -1);
    int out = 1;
    for (int k = 0; mesh && k < mesh->nv; k++) {
        const packs_mvertex *v = &mesh->v[k];
        float l = v->nrm[0] * v->nrm[0] + v->nrm[1] * v->nrm[1] + v->nrm[2] * v->nrm[2];
        float d = v->nrm[0] * v->pos[0] + v->nrm[1] * v->pos[1] + v->nrm[2] * v->pos[2];
        out &= l > 0.99f && l < 1.01f && d > 0;
    }
    CHECK(out, "cube normals: unit, outward");
    CHECK(packs_model_mesh(0) == mesh, "the mesh is kept");
    CHECK(packs_model_mesh(1) == NULL, "no mesh for the missing file");
    packs_unload();
}

/* [[model]] keys, and [[pose]] entries with the 2D map's pose pixels. */
static void test_poses_and_keys(const char *tmp)
{
    char dir[4096], path[4200];
    snprintf(dir, sizeof dir, "%s/packs/poses", tmp);
    snprintf(path, sizeof path, "mkdir -p '%s'", dir);
    CHECK(run(path) == 0, "mkdir");
    /* the pose: 32 x 16 console px at 4x; its right quarter transparent */
    static uint8_t img[128 * 64 * 4];
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 128; x++) {
            uint8_t *q = &img[(y * 128 + x) * 4];
            q[0] = (uint8_t)x;
            q[1] = (uint8_t)y;
            q[2] = 200;
            q[3] = x < 96 ? 255 : 0;
        }
    snprintf(path, sizeof path, "%s/pose.png", dir);
    CHECK(!pngio_write_rgba(path, img, 128, 64), "write the pose");
    snprintf(path, sizeof path, "%s/pack.toml", dir);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "[pack]\nversion = 1\n\n[[pose]]\nkey = \"01000002\"\nfile = \"pose.png\"\n"
                   "w = 32\nh = 16\nflip = \"h\"\n\n"
                   "[[model]]\nmap = 5\ncells = [2, 3, 4, 6]\nfile = \"x.glb\"\nscale = [1, 2, 3]\n"
                   "rotate = [0, 90, 0]\ncolor = [0.5, 1, 1, 1]\nfilter = \"linear\"\nhide_cells = true\n\n"
                   "[[model]]\ntiles = [1, 0x0123, 0x4124, 0x2133, 0x0134]\nsize = [2, 1]\nfile = \"y.glb\"\n\n"
                   "[[model]]\nfile = \"no_place.glb\"\n");
        fclose(f);
    }
    const char *dirs[1] = {dir};
    packs_load(dirs, 1);
    int n;
    const packs_model *m = packs_models(&n);
    CHECK(n == 2, "%d models (one with no place left out)", n);
    if (n == 2) {
        CHECK(m[0].map == 5 && m[0].cell_x == 2 && m[0].cell_y == 3 && m[0].cells_w == 3 &&
                  m[0].cells_h == 4 && m[0].scale[1] == 2 && m[0].scale[2] == 3 &&
                  m[0].rotate[1] == 90 && m[0].color[0] == 0.5f && m[0].linear && m[0].hide_cells,
              "model keys: cells, scale, rotate, color, filter, hide_cells");
        CHECK(m[1].map == -1 && m[1].n_tiles == 4 && m[1].tiles_layer == 1 && m[1].tiles[1] == 0x4124 &&
                  m[1].cells_w == 2 && m[1].cells_h == 1 && !m[1].linear,
              "model keys: tiles, size");
    }
    CHECK(packs_have_poses(), "a pose loaded");
    const packs_rect *r = packs_lookup_pose(0x01000002);
    CHECK(r && r->w == 128 && r->h == 64 && r->src_w == 32 && r->src_h == 16 && r->flags == PACKS_FLIP_H,
          "pose lookup");
    CHECK(packs_lookup_tile(0x01000002) == NULL, "poses are not tiles");
    int ne;
    const packs_rect *all = packs_entries(&ne);

    /* The frame of test_dump_and_load: OAM 0 and 1 (16x16 each, at 40,0 and
       56,0) are one object, its pose 32 x 16; two BG pixels inside its box,
       one behind it (z 2), one in front (z 12). */
    frame[5 * SCHED_WIDTH + 60] = bg_px(1, 1, 0);
    frame[5 * SCHED_WIDTH + 60].layer = 0x20;
    frame[6 * SCHED_WIDTH + 60] = bg_px(1, 1, 0);
    frame[6 * SCHED_WIDTH + 60].layer = 0xC0;
    frame[5 * SCHED_WIDTH + 45] = frame[5 * SCHED_WIDTH + 60];
    int x, y, w, h;
    const uint8_t sizes[2] = {8, 16}, both[2] = {0, 1};
    CHECK(!packs_oam_box(mem.oam, sizes, both, 2, &x, &y, &w, &h) && x == 40 && y == 0 && w == 32 && h == 16,
          "OAM box %d,%d %dx%d", x, y, w, h);
    packs_pose_place pl = {(int)(r - all), 32, 16, 0, 0, 2, {0, 1}};
    packs_set_poses(&pl, 1, sizes);
    packs_map_frame(frame, &mem, map);
    const uint16_t *e = &map[(3 * SCHED_WIDTH + 50) * 4];
    /* flipped: screen x 50 is texel 31 - 10 */
    CHECK(e[0] == pl.entry + 1 && e[1] == (21 | 3 << 8) && (e[2] & SCHED_PROV_HFLIP) && !(e[2] & 64),
          "pose pixel: entry %d texel %d,%d", e[0] - 1, e[1] & 255, e[1] >> 8);
    e = &map[(10 * SCHED_WIDTH + 63) * 4];
    CHECK(e[0] == pl.entry + 1 && (e[1] & 255) == 8, "the second OAM entry: the same picture");
    e = &map[(5 * SCHED_WIDTH + 60) * 4];
    CHECK(e[0] == pl.entry + 1 && (e[1] & 255) == 11 && (e[2] & 64),
          "behind the object: the pose's picture, over that pixel");
    e = &map[(6 * SCHED_WIDTH + 60) * 4];
    CHECK(e[0] != pl.entry + 1, "in front of the object: not the pose");
    e = &map[(5 * SCHED_WIDTH + 45) * 4];
    CHECK(e[0] != pl.entry + 1, "behind it, where the picture is transparent: not the pose");
    /* a box that doesn't match: the pieces keep their own */
    pl.w = 30;
    packs_set_poses(&pl, 1, sizes);
    packs_map_frame(frame, &mem, map);
    CHECK(map[(3 * SCHED_WIDTH + 50) * 4] != pl.entry + 1, "pose of another size: not used");
    packs_set_poses(NULL, 0, NULL);
    packs_unload();
    CHECK(!packs_have_poses() && packs_lookup_pose(0x01000002) == NULL, "unload: no poses");
}

/* A pack is data only: a manifest path that leaves the pack is refused. */
static void write_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void test_traversal(const char *tmp)
{
    char cmd[4400], root[4096], evil[4096], good[4096], path[4400], toml[4096];
    char abstmp[4096];
#ifdef _WIN32
    if (!_fullpath(abstmp, tmp, sizeof abstmp))
#else
    if (!realpath(tmp, abstmp))
#endif
        snprintf(abstmp, sizeof abstmp, "%s", tmp);
    snprintf(root, sizeof root, "%s/trav", abstmp);
    snprintf(evil, sizeof evil, "%s/evil", root);
    snprintf(good, sizeof good, "%s/good", root);
    snprintf(cmd, sizeof cmd, "mkdir -p '%s/tiles' '%s/models' '%s/tiles'", evil, evil, good);
    CHECK(run(cmd) == 0, "mkdir");
    static uint8_t px[8 * 8 * 4];
    memset(px, 0x55, sizeof px);
    snprintf(path, sizeof path, "%s/outside.png", root);
    CHECK(!pngio_write_rgba(path, px, 8, 8), "write outside.png");
    snprintf(path, sizeof path, "%s/tiles/ok.png", good);
    CHECK(!pngio_write_rgba(path, px, 8, 8), "write ok.png");
    snprintf(path, sizeof path, "%s/tiles/link.png", evil);
#ifndef _WIN32   /* no symlink case there: making one needs a privilege */
    CHECK(symlink("../../outside.png", path) == 0, "symlink");
#endif
    write_text(strcat(strcpy(path, root), "/outside.bin"), "xxxx");
    write_text(strcat(strcpy(path, evil), "/models/m.gltf"),
               "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"../../outside.bin\","
               "\"byteLength\":4}]}");

    const char *bad[] = {"../outside.png", "tiles/../../outside.png", "ABS", "tiles\\\\x.png",
                         "C:/x.png", "tiles/link.png"};
    char *p = toml;
    p += sprintf(p, "[pack]\nversion = 1\n");
    for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
        char abs[4200];
        snprintf(abs, sizeof abs, "%s/outside.png", root);
        p += sprintf(p, "\n[[tile]]\nkey = \"%016zx\"\nfile = \"%s\"\nw = 8\nh = 8\n", k + 1,
                     !strcmp(bad[k], "ABS") ? abs : bad[k]);
    }
    p += sprintf(p, "\n[[model]]\nfile = \"../outside.png\"\nmap = 0x1F0\n");
    p += sprintf(p, "\n[[model]]\nfile = \"models/m.gltf\"\nmap = 0x1F0\n");
    p += sprintf(p, "\n[[music]]\nsong = 1\nfile = \"../x.ogg\"\n");
    snprintf(path, sizeof path, "%s/pack.toml", evil);
    write_text(path, toml);

    const char *dirs[1] = {evil};
    packs_load(dirs, 1);
    int tiles = 0;
    for (uint64_t k = 1; k <= 6; k++)
        tiles += packs_lookup_tile(k) != NULL;
    CHECK(tiles == 0, "tiles naming paths outside the pack: %d loaded", tiles);
    int n = -1;
    const packs_model *pm = packs_models(&n);
    CHECK(n == 1 && pm[0].primitives == 0 && pm[0].meshes == 0,
          "model paths outside the pack: none kept, the glTF with an outside buffer has no mesh (%d)", n);
    packs_unload();
    music_load(dirs, 1);
    CHECK(music_file(1) == NULL, "music file outside the pack: no entry");
    music_unload();

    packs_model m;
    char err[512];
    memset(&m, 0, sizeof m);
    snprintf(path, sizeof path, "%s/models/m.gltf", evil);
    CHECK(packs_model_parse(path, &m, err, sizeof err) == -1 && strstr(err, "'..'"),
          "glTF buffer outside the pack: %s", err);

    /* glTF image outside the folder; a buffer that is a symlink out */
    write_text(strcat(strcpy(path, evil), "/models/img.gltf"),
               "{\"asset\":{\"version\":\"2.0\"},\"images\":[{\"uri\":\"../../outside.png\"}]}");
    snprintf(path, sizeof path, "%s/models/img.gltf", evil);
    CHECK(packs_model_parse(path, &m, err, sizeof err) == -1 && strstr(err, "'..'"),
          "glTF image outside the pack: %s", err);
#ifndef _WIN32
    snprintf(path, sizeof path, "%s/models/b.bin", evil);
    CHECK(symlink("../../outside.bin", path) == 0, "symlink b.bin");
    write_text(strcat(strcpy(path, evil), "/models/sym.gltf"),
               "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[{\"uri\":\"b.bin\",\"byteLength\":4}]}");
    snprintf(path, sizeof path, "%s/models/sym.gltf", evil);
    CHECK(packs_model_parse(path, &m, err, sizeof err) == -1 && strstr(err, "symlink"),
          "glTF buffer that is a symlink out: %s", err);
#endif

    snprintf(path, sizeof path, "%s/pack.toml", good);
    write_text(path, "[pack]\nversion = 1\n\n[[tile]]\nkey = \"00000000000000aa\"\n"
                     "file = \"tiles/ok.png\"\nw = 8\nh = 8\n"
                     "\n[[music]]\nsong = 2\nfile = \"a.ogg\"\n");
    dirs[0] = good;
    CHECK(packs_load(dirs, 1) == 1, "a normal entry still loads");
    packs_unload();
    music_load(dirs, 1);
    CHECK(music_file(2) != NULL, "a normal music entry still loads");
    music_unload();
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: test_packs SOURCE_DIR TMP_DIR\n");
        return 2;
    }
    char tmp[4096], cmd[4200];
    snprintf(tmp, sizeof tmp, "%s/test_packs.d", argv[2]);
    snprintf(cmd, sizeof cmd, "rm -rf '%s' && mkdir -p '%s'", tmp, tmp);
    if (run(cmd)) {
        fprintf(stderr, "cannot make %s\n", tmp);
        return 2;
    }
    test_png(tmp);
    test_keys();
    test_dump_and_load(tmp);
    test_models(argv[1]);
    test_poses_and_keys(tmp);
    test_traversal(tmp);
    printf("packs: %ld checks, %ld failed\n", checks, fails);
    return fails ? 1 : 0;
}
