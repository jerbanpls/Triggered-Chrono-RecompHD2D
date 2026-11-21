/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * First run of an engine-only build (firstrun.h). Everything game-specific
 * comes from DATA_DIR/game/GAME_ID/game.toml: the title shown, the ROM's
 * size and its SHA-1. The build tool runs as a child process on a pipe:
 * fork and exec on POSIX, CreateProcess on Windows (child_*). */
#include "firstrun.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <strings.h>
#include <sys/wait.h>
#endif

#include <SDL.h>

#include "bus.h"
#include "device.h"
#include "hd2d/osd_font.h"
#include "hid_deck.h"   /* port: InputPlumber's Deck pad */
#include "module.h"
#include "sched.h"
#include "platform.h"   /* port: ct_home; Windows: realpath, rename... */
#include "sha1.h"

#define W SCHED_WIDTH
#define H SCHED_HEIGHT
#define GLYPH_W 6
#define LINE_H 10
#define MARGIN 8
#define COLS ((W - 2 * MARGIN) / GLYPH_W)
#define ROWS 17           /* body rows, between the header and the footer */
#define PATHLEN 4096

/* ---- the status screen: a title, a headline, wrapped body lines and a
   footer, in the 5x7 font, on a dark ground ---- */

enum { C_TEXT, C_DIM, C_TITLE, C_BAD, C_GOOD, C_COUNT };
static const uint8_t palette[C_COUNT][3] = {   /* B G R */
    [C_TEXT] = {235, 235, 235}, [C_DIM] = {150, 140, 135}, [C_TITLE] = {120, 205, 250},
    [C_BAD] = {120, 120, 250},  [C_GOOD] = {150, 230, 150},
};

static struct {
    char title[COLS + 1], head[COLS + 1], foot[COLS + 1];
    uint8_t head_color;
    char body[ROWS][COLS + 1];
    uint8_t color[ROWS];
    int n;
} scr;
static uint8_t fb[W * H * 4];

static void scr_clear(const char *head, int color)
{
    scr.n = 0;
    snprintf(scr.head, sizeof scr.head, "%s", head);
    scr.head_color = (uint8_t)color;
    scr.foot[0] = 0;
}

static void scr_line(int color, const char *s)
{
    if (scr.n == ROWS) {   /* full: scroll, keeping the newest */
        memmove(scr.body[0], scr.body[1], sizeof scr.body[0] * (ROWS - 1));
        memmove(scr.color, scr.color + 1, ROWS - 1);
        scr.n--;
    }
    snprintf(scr.body[scr.n], COLS + 1, "%s", s);
    scr.color[scr.n++] = (uint8_t)color;
}

/* Add text, wrapped at COLS on spaces (long words are cut). */
static void scr_add(int color, const char *fmt, ...)
{
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    const char *p = text;
    do {
        size_t len = strlen(p), cut = len;
        if (len > COLS) {
            cut = COLS;
            while (cut > COLS / 2 && p[cut] != ' ')
                cut--;
            if (p[cut] != ' ')
                cut = COLS;
        }
        char line[COLS + 1];
        memcpy(line, p, cut);
        line[cut] = 0;
        scr_line(color, line);
        p += cut;
        while (*p == ' ')
            p++;
    } while (*p);
}

static void draw_text(int x, int y, int color, const char *s)
{
    const uint8_t *c = palette[color];
    for (; *s && x + 5 <= W; s++, x += GLYPH_W) {
        const uint8_t *rows = osd_glyph(*s);
        for (int r = 0; r < 7; r++)
            for (int b = 0; b < 5; b++)
                if (rows[r] & (0x10 >> b)) {
                    uint8_t *px = fb + ((size_t)(y + r) * W + x + b) * 4;
                    px[0] = c[0];
                    px[1] = c[1];
                    px[2] = c[2];
                }
    }
}

static const firstrun_opts *opt;

static void scr_show(int shot)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *px = fb + ((size_t)y * W + x) * 4;
            px[0] = (uint8_t)(34 + y / 16);   /* a dark blue, lighter toward the bottom */
            px[1] = (uint8_t)(22 + y / 24);
            px[2] = (uint8_t)(18 + y / 32);
            px[3] = 0;
        }
    int tl = (int)strlen(scr.title);
    draw_text((W - tl * GLYPH_W) / 2, MARGIN, C_TITLE, scr.title);
    for (int x = MARGIN; x < W - MARGIN; x++) {   /* a rule under the title */
        uint8_t *px = fb + ((size_t)(MARGIN + 10) * W + x) * 4;
        px[0] = 120, px[1] = 95, px[2] = 80;
    }
    draw_text(MARGIN, MARGIN + 16, scr.head_color, scr.head);
    for (int k = 0; k < scr.n; k++)
        draw_text(MARGIN, MARGIN + 32 + k * LINE_H, scr.color[k], scr.body[k]);
    draw_text(MARGIN, H - MARGIN - 7, C_DIM, scr.foot);
    if (opt->present)
        opt->present(fb);
    if (shot && opt->shot && opt->shot_path)
        opt->shot(fb, opt->shot_path);
}

/* ---- events ---- */

static SDL_GameController *pads[4];

typedef struct {
    int quit, key;
    int button;          /* port: the last controller button pressed, or -1 */
    SDL_Scancode sc;     /* the last key pressed */
    char *drop;   /* SDL_free it */
} events;

#define EVENTS_INIT {0, 0, -1, SDL_SCANCODE_UNKNOWN, NULL}

/* port: the "confirm" and "back" answers, by controller (A, B: bottom and
   right face buttons) or key (Enter, Escape; Steam Input's desktop layout
   sends those for A and B). */
static int ev_yes(const events *e)
{
    return e->button == SDL_CONTROLLER_BUTTON_A || e->sc == SDL_SCANCODE_RETURN ||
           e->sc == SDL_SCANCODE_KP_ENTER || e->sc == SDL_SCANCODE_SPACE;
}

/* The quit hint for footers: no keyboard in Steam's Game Mode. */
static const char *quit_hint(void)
{
    return device_game_mode(NULL) ? "Steam button: quit" : "Ctrl+Q quits";
}

static void pump(events *e)
{
    SDL_Event ev;
    hid_deck_update();   /* port: InputPlumber's Deck pad (hid_deck.h) */
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT) {
            e->quit = 1;
        } else if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
            if (ev.key.keysym.scancode == SDL_SCANCODE_Q && (ev.key.keysym.mod & KMOD_CTRL))
                e->quit = 1;
            else {
                e->key = 1;
                e->sc = ev.key.keysym.scancode;
            }
        } else if (ev.type == SDL_CONTROLLERBUTTONDOWN) {
            e->key = 1;
            e->button = ev.cbutton.button;
        } else if (ev.type == SDL_FINGERDOWN) {
            e->key = 1;   /* a tap counts too (phones) */
        } else if (ev.type == SDL_CONTROLLERDEVICEADDED) {
            for (int k = 0; k < 4; k++)
                if (!pads[k]) {
                    pads[k] = SDL_GameControllerOpen(ev.cdevice.which);
                    break;
                }
        } else if (ev.type == SDL_DROPFILE) {
            if (!e->drop)
                e->drop = ev.drop.file;
            else
                SDL_free(ev.drop.file);
        }
    }
}

static void close_pads(void)
{
    for (int k = 0; k < 4; k++)
        if (pads[k]) {
            SDL_GameControllerClose(pads[k]);
            pads[k] = NULL;
        }
}

/* ---- the game's description: top-level keys of game.toml ---- */

static struct {
    char name[COLS + 1];
    long rom_size;
    char sha1[41];
} game;

static int read_game_toml(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '[')
            break;   /* tables: past the top-level keys */
        char key[64], *eq = strchr(p, '=');
        size_t kl = eq ? (size_t)(eq - p) : 0;
        while (kl && (p[kl - 1] == ' ' || p[kl - 1] == '\t'))
            kl--;
        if (!eq || !kl || kl >= sizeof key)
            continue;
        memcpy(key, p, kl);
        key[kl] = 0;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t')
            v++;
        char str[256] = "";
        if (*v == '"') {
            char *end = strchr(v + 1, '"');
            if (end && (size_t)(end - v - 1) < sizeof str) {
                memcpy(str, v + 1, (size_t)(end - v - 1));
                str[end - v - 1] = 0;
            }
        }
        if (!strcmp(key, "name"))
            snprintf(game.name, sizeof game.name, "%s", str);
        else if (!strcmp(key, "rom_size"))
            game.rom_size = strtol(v, NULL, 0);
        else if (!strcmp(key, "rom_sha1") && strlen(str) == 40)
            for (int k = 0; k <= 40; k++)
                game.sha1[k] = (char)(str[k] >= 'A' && str[k] <= 'F' ? str[k] - 'A' + 'a' : str[k]);
    }
    fclose(f);
    return 0;
}

/* ---- child processes ---- */

#ifdef _WIN32
/* The folder of the running exe (UTF-8, no trailing separator) and the
   exe's own file name: the package is unpacked anywhere, and its Python and
   zig sit next to the exe, so nothing needs PATH. */
static char exe_dir[PATHLEN], exe_file[128];

static void exe_init(void)
{
    wchar_t w[MAX_PATH * 2];
    char u[PATHLEN];
    DWORD n = GetModuleFileNameW(NULL, w, (DWORD)(sizeof w / sizeof w[0]));
    exe_dir[0] = exe_file[0] = 0;
    if (!n || n >= sizeof w / sizeof w[0] ||
        !WideCharToMultiByte(CP_UTF8, 0, w, -1, u, (int)sizeof u, NULL, NULL))
        return;
    char *sep = strrchr(u, '\\');
    if (!sep)
        return;
    snprintf(exe_file, sizeof exe_file, "%s", sep + 1);
    *sep = 0;
    snprintf(exe_dir, sizeof exe_dir, "%s", u);
}

/* %USERPROFILE% (not $HOME, which only MSYS shells set). */
static const char *home_dir(void)
{
    const char *h = getenv("USERPROFILE");
    return h && *h ? h : ct_home();
}

/* File name part of a path: either kind of separator on Windows. */
static const char *base_name(const char *path)
{
    const char *a = strrchr(path, '/'), *b = strrchr(path, '\\');
    return a && b ? (a > b ? a : b) + 1 : a ? a + 1 : b ? b + 1 : path;
}

/* "<exe dir>\<rel>" if that file exists, else NULL. */
static const char *bundled(char *out, size_t n, const char *rel)
{
    if (!exe_dir[0])
        return NULL;
    snprintf(out, n, "%s\\%s", exe_dir, rel);
    return access(out, F_OK) == 0 ? out : NULL;
}
#else
static const char *base_name(const char *path)
{
    const char *a = strrchr(path, '/');
    return a ? a + 1 : path;
}
#endif

#ifdef _WIN32
/* ct_sdl_engine.exe is a GUI program (no console window for players). Started
   from a console or with its output redirected (--frames test runs), it keeps
   that output; started from the Explorer, its standard handles are not valid
   and what would go to stdout and stderr goes to ct_sdl.log in the
   preferences folder instead (%APPDATA%\\ct-recomp\\GAME\\). */
void firstrun_log_redirect(const char *game)
{
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
        return;
    char *pref = SDL_GetPrefPath("ct-recomp", game);
    if (!pref)
        return;
    char path[4096];
    snprintf(path, sizeof path, "%sct_sdl.log", pref);
    SDL_free(pref);
    FILE *f = freopen(path, "w", stderr);
    if (f)
        setvbuf(stderr, NULL, _IONBF, 0);
    if (freopen(path, "a", stdout))
        setvbuf(stdout, NULL, _IOLBF, 0);
}
#endif

static const char *python(void)
{
    const char *p = getenv("CT_PYTHON");
    if (p && *p)
        return p;
#ifdef _WIN32
    static char b[PATHLEN];
    const char *bp = bundled(b, sizeof b, "python\\python.exe");
    return bp ? bp : "python";   /* not bundled: python.org and the Store install python, not python3 */
#else
    return "python3";
#endif
}

#ifdef _WIN32
typedef struct {
    HANDLE proc;
    HANDLE rd;
    char buf[2048];
    size_t len;
} child;

/* argv as one command line, each argument quoted by the rules of
   CommandLineToArgvW (backslashes only count before a quote). */
static int cmdline(char *out, size_t n, char *const argv[])
{
    size_t o = 0;
#define PUT(ch) do { if (o + 1 >= n) return -1; out[o++] = (ch); } while (0)
    for (int k = 0; argv[k]; k++) {
        if (k)
            PUT(' ');
        PUT('"');
        size_t bs = 0;
        for (const char *a = argv[k]; *a; a++) {
            if (*a == '\\') {
                bs++;
                continue;
            }
            if (*a == '"')
                bs = bs * 2 + 1;
            for (; bs; bs--)
                PUT('\\');
            PUT(*a);
        }
        for (bs *= 2; bs; bs--)
            PUT('\\');
        PUT('"');
    }
#undef PUT
    out[o] = 0;
    return 0;
}

/* Start argv with its stdout and stderr on a pipe. */
static int child_start(child *c, char *const argv[])
{
    char line[8192];
    if (cmdline(line, sizeof line, argv))
        return -1;
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0))
        return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi;
    BOOL ok = CreateProcessA(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return -1;
    }
    CloseHandle(pi.hThread);
    c->proc = pi.hProcess;
    c->rd = rd;
    c->len = 0;
    return 0;
}

/* Bytes read, 0 at the end of the output, -1 when none are ready yet. */
static long child_recv(child *c, char *buf, size_t n)
{
    DWORD avail = 0, got = 0;
    if (!PeekNamedPipe(c->rd, NULL, 0, NULL, &avail, NULL))
        return 0;   /* the child and its handles are gone: the end */
    if (!avail)
        return -1;
    if (!ReadFile(c->rd, buf, avail < n ? avail : (DWORD)n, &got, NULL))
        return 0;
    return (long)got;
}

static void child_kill(child *c)
{
    TerminateProcess(c->proc, 1);
}

static int child_wait(child *c)
{
    DWORD code = 1;
    CloseHandle(c->rd);
    WaitForSingleObject(c->proc, INFINITE);
    GetExitCodeProcess(c->proc, &code);
    CloseHandle(c->proc);
    return (int)code;
}
#else
typedef struct {
    pid_t pid;
    int fd;
    char buf[2048];
    size_t len;
} child;

/* Start argv with its stdout and stderr on a pipe. */
static int child_start(child *c, char *const argv[])
{
    int p[2];
    if (pipe(p))
        return -1;
    pid_t pid = fork();
    if (pid < 0) {
        close(p[0]);
        close(p[1]);
        return -1;
    }
    if (pid == 0) {
        dup2(p[1], 1);
        dup2(p[1], 2);
        close(p[0]);
        close(p[1]);
        execvp(argv[0], argv);
        static const char msg[] = "cannot start the build tool (is python3 installed?)\n";
        if (write(2, msg, sizeof msg - 1) < 0)
            _exit(127);
        _exit(127);
    }
    close(p[1]);
    fcntl(p[0], F_SETFL, fcntl(p[0], F_GETFL) | O_NONBLOCK);
    fcntl(p[0], F_SETFD, FD_CLOEXEC);
    c->pid = pid;
    c->fd = p[0];
    c->len = 0;
    return 0;
}

/* Bytes read, 0 at the end of the output, -1 when none are ready yet. */
static long child_recv(child *c, char *buf, size_t n)
{
    ssize_t r = read(c->fd, buf, n);
    if (r < 0)
        return errno == EAGAIN || errno == EINTR ? -1 : 0;
    return (long)r;
}

static void child_kill(child *c)
{
    kill(c->pid, SIGTERM);
}

static int child_wait(child *c)
{
    int st = 0;
    close(c->fd);
    while (waitpid(c->pid, &st, 0) < 0 && errno == EINTR)
        ;
    return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
}
#endif

/* Hand each complete line read so far to on_line. Returns 1 at EOF. */
static int child_read(child *c, void (*on_line)(char *))
{
    for (;;) {
        if (c->len >= sizeof c->buf - 1)   /* can't happen (flushed below); keeps the size sane */
            c->len = 0;
        long n = child_recv(c, c->buf + c->len, sizeof c->buf - 1 - c->len);
        if (n < 0)
            return 0;
        int eof = n == 0;
        c->len += (size_t)n;
        char *start = c->buf, *nl;
        while ((nl = memchr(start, '\n', c->len - (size_t)(start - c->buf)))) {
            *nl = 0;
            on_line(start);
            start = nl + 1;
        }
        c->len -= (size_t)(start - c->buf);
        memmove(c->buf, start, c->len);
        if (c->len == sizeof c->buf - 1 || (eof && c->len)) {   /* overlong or unterminated */
            c->buf[c->len] = 0;
            on_line(c->buf);
            c->len = 0;
        }
        if (eof)
            return 1;
    }
}

static void log_line(char *line)
{
    fprintf(stderr, "ct_sdl: %s\n", line);
}

/* ---- the ROM ---- */

static char rom_sha1[41];

/* Check path against game.toml: 0, or -1 with the reason in why. */
static int check_rom(const char *path, char *why, size_t n)
{
    char base[128];   /* the file name, enough to recognize it */
    snprintf(base, sizeof base, "%.127s", base_name(path));
    struct stat st;
    if (stat(path, &st) || !S_ISREG(st.st_mode)) {
        snprintf(why, n, "Not a file: %s", base);
        return -1;
    }
    if (st.st_size != game.rom_size || game.rom_size != (long)CT_ROM_SIZE) {
        snprintf(why, n, "%s is %ld bytes, expected %ld.%s", base, (long)st.st_size,
                 game.rom_size, st.st_size == game.rom_size + 512
                                    ? " It has a 512-byte copier header: remove it." : "");
        return -1;
    }
    uint8_t *data = malloc((size_t)st.st_size);
    FILE *f = data ? fopen(path, "rb") : NULL;
    size_t got = f ? fread(data, 1, (size_t)st.st_size, f) : 0;
    if (f)
        fclose(f);
    if (got != (size_t)st.st_size) {
        free(data);
        snprintf(why, n, "Cannot read %s", base);
        return -1;
    }
    sha1_hex(data, got, rom_sha1);
    free(data);
    if (game.sha1[0] && strcmp(rom_sha1, game.sha1)) {
        snprintf(why, n, "%s is not the expected ROM (SHA-1 %.12s...).", base, rom_sha1);
        return -1;
    }
    if (!game.sha1[0]) {   /* no SHA-1 in game.toml: ask the game's own check */
        char script[PATHLEN], gdir[PATHLEN];
        snprintf(script, sizeof script, "%s/tools/check_rom.py", opt->data_dir);
        snprintf(gdir, sizeof gdir, "%s/game/%s", opt->data_dir, opt->game_id);
        char *argv[] = {(char *)python(), script, gdir, (char *)path, NULL};
        child c;
        if (child_start(&c, argv) == 0) {
            while (!child_read(&c, log_line))
                SDL_Delay(5);
            if (child_wait(&c) == 0)
                return 0;
        }
        snprintf(why, n, "%s did not pass tools/check_rom.py.", base);
        return -1;
    }
    return 0;
}

/* ---- files in the preferences folder ---- */

static char pref[PATHLEN - 64];   /* room for a file name after it */

static void pref_file(char *out, size_t n, const char *name)
{
    snprintf(out, n, "%s%s", pref, name);
}

static void remember_rom(const char *path)
{
    char abs[PATHLEN], file[PATHLEN];
    if (!realpath(path, abs))
        snprintf(abs, sizeof abs, "%s", path);
    pref_file(file, sizeof file, "rom_path");
    FILE *f = fopen(file, "w");
    if (f) {
        fprintf(f, "%s\n", abs);
        fclose(f);
    }
}

static int remembered_rom(char *out, size_t n)
{
    char file[PATHLEN];
    pref_file(file, sizeof file, "rom_path");
    FILE *f = fopen(file, "r");
    if (!f)
        return -1;
    int ok = fgets(out, (int)n, f) != NULL;
    fclose(f);
    out[strcspn(out, "\r\n")] = 0;
    return ok && *out ? 0 : -1;
}

/* ---- port: looking for the ROM in the usual folders ----

   Documents, Downloads, the AppImage's folder and the per-game folder, to
   SCAN_DEPTH levels of subfolders and at most SCAN_MAX_ENTRIES directory
   entries in all; only .sfc / .smc files of the ROM's exact size are read
   and hashed. A handheld in Game Mode has no file manager to drop a file
   from. */
#define SCAN_DEPTH 2
#define SCAN_MAX_ENTRIES 2000
#define SCAN_DIRS 6

static char scan_dir[SCAN_DIRS][PATHLEN];
static int n_scan_dirs, scan_app = -1;   /* scan_app: the AppImage's folder */

static void scan_add(const char *a, const char *b)
{
    if (!a || !*a || n_scan_dirs == SCAN_DIRS)
        return;
    char d[PATHLEN];
    snprintf(d, sizeof d, "%s%s", a, b ? b : "");
    size_t l = strlen(d);
#ifdef _WIN32
    while (l > 3 && (d[l - 1] == '/' || d[l - 1] == '\\'))   /* (C:\ keeps its slash) */
        d[--l] = 0;
#else
    while (l > 1 && d[l - 1] == '/')
        d[--l] = 0;
#endif
    for (int k = 0; k < n_scan_dirs; k++)
        if (!strcmp(scan_dir[k], d))
            return;
    snprintf(scan_dir[n_scan_dirs++], PATHLEN, "%s", d);
}

static void scan_dirs_init(void)
{
    n_scan_dirs = 0;
    scan_app = -1;
#ifdef _WIN32
    /* The user's Documents and Downloads, then the folder of the exe. */
    const char *home = home_dir();
    scan_add(home, "\\Documents");
    scan_add(home, "\\Downloads");
    if (exe_dir[0]) {
        int before = n_scan_dirs;
        scan_add(exe_dir, NULL);
        if (n_scan_dirs > before)
            scan_app = before;
    }
    scan_add(pref, NULL);
    return;
#else
    const char *home = ct_home();
    scan_add(getenv("XDG_DOCUMENTS_DIR"), NULL);
    scan_add(home, "/Documents");
    scan_add(getenv("XDG_DOWNLOAD_DIR"), NULL);
    scan_add(home, "/Downloads");
    const char *app = getenv("APPIMAGE");   /* set by the AppImage runtime */
    if (app && strrchr(app, '/')) {
        char d[PATHLEN];
        snprintf(d, sizeof d, "%.*s", (int)(strrchr(app, '/') - app), app);
        int before = n_scan_dirs;
        scan_add(d[0] ? d : "/", NULL);
        if (n_scan_dirs > before)
            scan_app = before;
    }
    scan_add(pref, NULL);
#endif
}

/* Files already hashed and rejected, so a rescan doesn't read them again. */
static struct { dev_t dev; ino_t ino; time_t mtime; } rejected[64];
static int n_rejected;

typedef struct {
    int entries;
    char headered[PATHLEN];   /* a file of the ROM's size plus 512, if any */
} scan_state;

static int has_rom_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (!strcasecmp(dot, ".sfc") || !strcasecmp(dot, ".smc"));
}

static int scan_tree(const char *dir, int depth, scan_state *st, char *found, size_t n)
{
    DIR *d = opendir(dir);
    if (!d)
        return 0;
    struct dirent *e;
    int hit = 0;
    while (!hit && st->entries < SCAN_MAX_ENTRIES && (e = readdir(d))) {
        if (e->d_name[0] == '.')
            continue;   /* ., .., hidden files and folders */
        st->entries++;
        char path[PATHLEN];
        if (snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
            continue;
        struct stat ls, fs;
        if (lstat(path, &ls))
            continue;
        if (S_ISDIR(ls.st_mode)) {   /* not through symlinks: no loops */
#ifdef _WIN32
            /* the package's own folders (zig alone holds thousands of files) */
            static const char *const own[] = {"python", "zig", "tools", "runtime", "game",
                                              "recomp", "licenses"};
            int skip = 0;
            if (!depth && exe_dir[0] && !strcmp(dir, exe_dir))
                for (size_t k = 0; k < sizeof own / sizeof own[0]; k++)
                    skip |= !strcasecmp(e->d_name, own[k]);
            if (skip)
                continue;
#endif
            if (depth < SCAN_DEPTH)
                hit = scan_tree(path, depth + 1, st, found, n);
            continue;
        }
        if (!has_rom_ext(e->d_name) || stat(path, &fs) || !S_ISREG(fs.st_mode))
            continue;
        if (fs.st_size == game.rom_size + 512 && !st->headered[0])
            snprintf(st->headered, sizeof st->headered, "%s", path);
        if (fs.st_size != game.rom_size)
            continue;
        int seen = 0;
        for (int k = 0; k < n_rejected && !seen; k++)
            seen = rejected[k].dev == fs.st_dev && rejected[k].ino == fs.st_ino &&
                   rejected[k].mtime == fs.st_mtime;
        if (seen)
            continue;
        char why[512];
        if (!check_rom(path, why, sizeof why)) {
            snprintf(found, n, "%s", path);
            hit = 1;
        } else {
            fprintf(stderr, "ct_sdl: search: %s\n", why);
            int k = n_rejected < 64 ? n_rejected++ : 63;
            rejected[k].dev = fs.st_dev;
            rejected[k].ino = fs.st_ino;
            rejected[k].mtime = fs.st_mtime;
        }
    }
    closedir(d);
    return hit;
}

/* 0 with the ROM's path in found, or -1. headered (may be NULL) gets a
   file that would match but for a copier header. */
static int scan_for_rom(char *found, size_t n, char *headered, size_t hn)
{
    scan_state st = {0, ""};
    int hit = 0;
    for (int k = 0; k < n_scan_dirs && !hit; k++)
        hit = scan_tree(scan_dir[k], 0, &st, found, n);
    fprintf(stderr, "ct_sdl: search: %d entries in %d folders: %s\n", st.entries, n_scan_dirs,
            hit ? found : "no ROM");
    if (headered)
        snprintf(headered, hn, "%s", st.headered);
    return hit ? 0 : -1;
}

/* The module is fresh if it exists and its stamp names this ROM and this
   engine's module ABI. */
static int module_fresh(const char *module)
{
    char file[PATHLEN], line[128], sha[41] = "";
    unsigned abi = 0;
    if (access(module, R_OK))
        return 0;
    pref_file(file, sizeof file, "ct_game.stamp");
    FILE *f = fopen(file, "r");
    while (f && fgets(line, sizeof line, f)) {
        if (sscanf(line, "rom_sha1=%40s", sha) == 1)
            continue;
        sscanf(line, "module_abi=%u", &abi);
    }
    if (f)
        fclose(f);
    return !strcmp(sha, rom_sha1) && abi == CT_MODULE_ABI;
}

static void stamp_write(int ok)
{
    char file[PATHLEN];
    pref_file(file, sizeof file, "ct_game.stamp");
    if (!ok) {
        remove(file);
        return;
    }
    FILE *f = fopen(file, "w");
    if (f) {
        fprintf(f, "rom_sha1=%s\nmodule_abi=%u\n", rom_sha1, CT_MODULE_ABI);
        fclose(f);
    }
}

/* ---- the screens ---- */

/* Wait for the ROM: put in one of the searched folders (searched again on
   A or Enter, and every few seconds), or dropped on the window. 0 with
   path set, or FIRSTRUN_QUIT. */
static int wait_for_drop(char *path, size_t n, const char *problem)
{
    char why[512], headered[PATHLEN] = "";
    snprintf(why, sizeof why, "%s", problem ? problem : "");
    int shot = 1, searches = 1, gm = device_game_mode(NULL);
    Uint32 last_scan = SDL_GetTicks();
    for (;;) {
        scr_clear("No ROM found. Put your ROM file in:", C_TEXT);
        /* The folders, short: relative to the home folder (the font has
           no ~). The last one, the per-game folder, isn't shown. */
#ifdef _WIN32
        const char *home = home_dir();
#else
        const char *home = ct_home();
#endif
        size_t hl = home ? strlen(home) : 0;
        char where[1024] = "", app[PATHLEN] = "";
        for (int k = 0; k < n_scan_dirs - 1; k++) {
            const char *d = scan_dir[k];
            if (hl && !strncmp(d, home, hl) && (d[hl] == '/' || d[hl] == '\\' || !d[hl]))
                d = d[hl] ? d + hl + 1 : "the home folder";
            if (k == scan_app)
                snprintf(app, sizeof app, "%s", d);
            else
                snprintf(where + strlen(where), sizeof where - strlen(where), "%s%s",
                         where[0] ? ", " : "", d);
        }
        /* Long paths would push the rest off the screen: name the last
           folder only. */
        if (app[0] && strlen(app) > COLS - 28 && base_name(app) != app)
            memmove(app, base_name(app), strlen(base_name(app)) + 1);
        scr_add(C_TITLE, "  %s", where);
        if (home && hl <= COLS - 24)
            scr_add(C_DIM, "  in your home folder, %s", home);
        else
            scr_add(C_DIM, "  in your home folder");
#ifdef _WIN32
        if (app[0])
            scr_add(C_TITLE, "  or next to ct_sdl_engine.exe, in %s", app);
#else
        if (app[0])
            scr_add(C_TITLE, "  or next to the AppImage, in %s", app);
#endif
        scr_line(C_TEXT, "");
        scr_add(C_DIM, "%s, headerless, %ld bytes, SHA-1:", game.name, game.rom_size);
        scr_add(C_DIM, "%s", game.sha1[0] ? game.sha1 : "(checked by tools/check_rom.py)");
        scr_add(C_DIM, "The game code is built from it here, once. Nothing is sent anywhere.");
        if (headered[0]) {
            scr_line(C_TEXT, "");
            scr_add(C_BAD, "%s has a 512-byte copier header: remove it.",
                    base_name(headered));
        } else if (why[0]) {
            scr_line(C_TEXT, "");
            scr_add(C_BAD, "%s", why);
        }
        if (!gm)
            scr_add(C_DIM, "Or drop the file on this window, or start with --rom FILE.");
        scr_add(C_DIM, "Searched %d time%s.", searches, searches == 1 ? "" : "s");
        snprintf(scr.foot, sizeof scr.foot, "A: search again   %s", quit_hint());
        scr_show(shot);
        shot = 0;
        events e = EVENTS_INIT;
        pump(&e);
        if (e.quit) {
            SDL_free(e.drop);
            return FIRSTRUN_QUIT;
        }
        if (e.drop) {
            snprintf(path, n, "%s", e.drop);
            SDL_free(e.drop);
            if (!check_rom(path, why, sizeof why))
                return 0;
            fprintf(stderr, "ct_sdl: %s\n", why);
            headered[0] = 0;
            shot = 1;
        }
        if (ev_yes(&e) || SDL_GetTicks() - last_scan > 5000) {
            char h[PATHLEN];
            int asked = ev_yes(&e);
            if (!scan_for_rom(path, n, h, sizeof h))
                return 0;
            searches++;
            last_scan = SDL_GetTicks();
            if (strcmp(h, headered) || asked) {
                snprintf(headered, sizeof headered, "%s", h);
                shot = 1;
            }
        }
        SDL_Delay(30);
    }
}

/* The build's progress: build_module.py's step lines, newest last. */
#define MAX_STEPS 12
#define MAX_TAIL 6
static char steps[MAX_STEPS][COLS + 1], tail[MAX_TAIL][160];   /* tail: other output */
static int n_steps, n_tail, new_step;

/* in, with every word containing a slash cut to its last part: file
   names, not paths, fit on the screen. */
static void short_paths(const char *in, char *out, size_t n)
{
    size_t o = 0;
    while (*in && o + 1 < n) {
        size_t len = strcspn(in, " ");
        const char *w = in, *slash = NULL;
        for (size_t k = 0; k < len; k++)
            if (in[k] == '/')
                slash = in + k;
        if (slash && slash + 1 < in + len)
            w = slash + 1;
        while (w < in + len && o + 1 < n)
            out[o++] = *w++;
        in += len;
        while (*in == ' ' && o + 1 < n)
            out[o++] = *in++;
    }
    out[o] = 0;
}

static void on_build_line(char *line)
{
    fprintf(stderr, "ct_sdl: %s\n", line);
    static const char prefix[] = "build_module: ";
    if (!strncmp(line, prefix, sizeof prefix - 1)) {
        if (n_steps == MAX_STEPS) {
            memmove(steps[0], steps[1], sizeof steps[0] * (MAX_STEPS - 1));
            n_steps--;
        }
        short_paths(line + sizeof prefix - 1, steps[n_steps++], COLS - 1);
        new_step = 1;
    }
    if (*line && (strncmp(line, prefix, sizeof prefix - 1) || strstr(line, "failed"))) {
        if (n_tail == MAX_TAIL) {
            memmove(tail[0], tail[1], sizeof tail[0] * (MAX_TAIL - 1));
            n_tail--;
        }
        short_paths(line, tail[n_tail++], sizeof tail[0]);
    }
}

static void build_screen(unsigned secs)
{
    scr_clear("Building the game code from your ROM", C_TEXT);
    scr_add(C_DIM, "First run only: this takes about a minute.");
    scr_line(C_TEXT, "");
    for (int k = 0; k < n_steps; k++)
        scr_add(k == n_steps - 1 ? C_TEXT : C_DIM, "%s %s", k == n_steps - 1 ? ">" : " ",
                steps[k]);
    snprintf(scr.foot, sizeof scr.foot, "%u s   %s", secs,
             device_game_mode(NULL) ? "Steam button: stop" : "Ctrl+Q stops");
}

/* Run tools/build_module.py. 0 when it built the module, 1 when it
   failed, FIRSTRUN_QUIT. */
static int build(const char *rom)
{
    char script[PATHLEN], gdir[PATHLEN], out[PATHLEN];
    snprintf(script, sizeof script, "%s/tools/build_module.py", opt->data_dir);
    snprintf(gdir, sizeof gdir, "%s/game/%s", opt->data_dir, opt->game_id);
    snprintf(out, sizeof out, "%s", pref);
    size_t ol = strlen(out);
    if (ol > 1 && (out[ol - 1] == '/' || out[ol - 1] == '\\'))
        out[ol - 1] = 0;
#ifdef _WIN32
    /* The C compiler is the package's zig (CT_ZIG overrides) and the module
       links against the import library of this very exe: a module binds to
       the program's name (libct_sdl_engine.dll.a for ct_sdl_engine.exe). */
    static char zigbuf[PATHLEN], cc[PATHLEN + 64], implib[PATHLEN];
    const char *zig = getenv("CT_ZIG");
    if (!zig || !*zig)
        zig = bundled(zigbuf, sizeof zigbuf, "zig\\zig.exe");
    snprintf(cc, sizeof cc, "\"%s\" cc -target x86_64-windows-gnu", zig ? zig : "zig");
    char stem[128];
    snprintf(stem, sizeof stem, "%s", exe_file);
    if (strrchr(stem, '.'))
        *strrchr(stem, '.') = 0;
    snprintf(implib, sizeof implib, "%s\\lib%s.dll.a", exe_dir, stem[0] ? stem : "ct_sdl_engine");
    char *argv[] = {(char *)python(), "-u", script, "--game", gdir, "--rom", (char *)rom,
                    "--out", out, "--native", "--cc", cc, "--engine-lib", implib, NULL};
#else
    char *argv[] = {(char *)python(), "-u", script, "--game", gdir, "--rom", (char *)rom,
                    "--out", out, "--native", NULL};
#endif
    n_steps = n_tail = 0;
    stamp_write(0);
    fprintf(stderr, "ct_sdl: building the game module: %s %s --game %s --rom %s --out %s "
                    "--native\n", argv[0], script, gdir, rom, out);
    child c;
    if (access(script, R_OK) || child_start(&c, argv)) {
        on_build_line(access(script, R_OK) ? "the build tool is missing: tools/build_module.py"
#ifdef _WIN32
                                           : "cannot start python\\python.exe from this folder (set CT_PYTHON)");
#else
                                           : "cannot start the build tool");
#endif
        return 1;
    }
    Uint32 t0 = SDL_GetTicks();
    new_step = 1;
    for (;;) {
        events e = EVENTS_INIT;
        pump(&e);
        SDL_free(e.drop);
        if (e.quit) {
            child_kill(&c);
            child_wait(&c);
            return FIRSTRUN_QUIT;
        }
        int eof = child_read(&c, on_build_line);
        build_screen((SDL_GetTicks() - t0) / 1000);
        scr_show(new_step);
        new_step = 0;
        if (eof)
            break;
        SDL_Delay(30);
    }
    int status = child_wait(&c);
    fprintf(stderr, "ct_sdl: build_module.py exited with %d after %u s\n", status,
            (SDL_GetTicks() - t0) / 1000);
    return status == 0 ? 0 : 1;
}

/* The build or the load failed: offer the interpreter, or another try.
   FIRSTRUN_INTERP, FIRSTRUN_QUIT or OFFER_RETRY. */
#define OFFER_RETRY 100
static int offer_interp(const char *what)
{
    if (opt->build_only) {   /* nobody to ask */
        fprintf(stderr, "ct_sdl: %s\n", what);
        return FIRSTRUN_ERROR;
    }
    int shot = 1;
    for (;;) {
        scr_clear(what, C_BAD);
        for (int k = 0; k < n_tail; k++)
            scr_add(C_DIM, "%s", tail[k]);
        scr_line(C_TEXT, "");
        scr_add(C_TEXT, "B: play without it. It is slower: the interpreter runs all of the "
                        "game.");
        scr_add(C_TEXT, "A: try again.");
        snprintf(scr.foot, sizeof scr.foot, "%s",
                 device_game_mode(NULL) ? quit_hint() : "Details: see the terminal. Ctrl+Q quits.");
        scr_show(shot);
        shot = 0;
        events e = EVENTS_INIT;
        pump(&e);
        SDL_free(e.drop);
        if (e.quit)
            return FIRSTRUN_QUIT;
        if (ev_yes(&e))
            return OFFER_RETRY;
        if (e.key)   /* B, Escape, any other key or a tap */
            return FIRSTRUN_INTERP;
        SDL_Delay(30);
    }
}

int firstrun(const firstrun_opts *o)
{
    opt = o;
    char toml[PATHLEN];
#ifdef _WIN32
    /* The data folder is given relative to the exe ("."): the package can be
       unpacked anywhere. */
    static firstrun_opts abs_opts;
    static char data[PATHLEN];
    exe_init();
    abs_opts = *o;
    const char *dd = o->data_dir;
    if (exe_dir[0] && dd && *dd && dd[0] != '/' && dd[0] != '\\' && !(dd[0] && dd[1] == ':')) {
        snprintf(data, sizeof data, "%s\\%s", exe_dir, strcmp(dd, ".") ? dd : "");
        size_t dl = strlen(data);
        if (dl && data[dl - 1] == '\\')
            data[dl - 1] = 0;
        abs_opts.data_dir = data;
        opt = &abs_opts;
        o = opt;
    }
#endif
    snprintf(toml, sizeof toml, "%s/game/%s/game.toml", o->data_dir, o->game_id);
    if (read_game_toml(toml) || !game.rom_size) {
        fprintf(stderr, "ct_sdl: no game description at %s (--data-dir DIR, --game ID)\n", toml);
        return FIRSTRUN_ERROR;
    }
    snprintf(scr.title, sizeof scr.title, "%s", game.name[0] ? game.name : o->game_id);
    char *p = SDL_GetPrefPath("ct-recomp", o->game_id);
    if (!p) {
        fprintf(stderr, "ct_sdl: no preferences folder: %s\n", SDL_GetError());
        return FIRSTRUN_ERROR;
    }
    snprintf(pref, sizeof pref, "%s", p);
    SDL_free(p);
    for (int k = 0; k < SDL_NumJoysticks() && k < 4; k++)
        if (SDL_IsGameController(k))
            pads[k] = SDL_GameControllerOpen(k);

    /* The ROM: --rom, $CT_ROM, the remembered one, else a dropped file. */
    static char rom[PATHLEN], why[512], problem[512];   /* problem: the first one */
    int found = 0;
    /* --build-only --rom FILE: that file or nothing. */
    int n_src = o->build_only && o->rom_arg ? 1 : 3;
    for (int src = 0; src < n_src && !found; src++) {
        if (src == 0 && o->rom_arg)
            snprintf(rom, sizeof rom, "%s", o->rom_arg);
        else if (src == 1 && getenv("CT_ROM") && *getenv("CT_ROM"))
            snprintf(rom, sizeof rom, "%s", getenv("CT_ROM"));
        else if (src != 2 || remembered_rom(rom, sizeof rom))
            continue;
        static const char *const from[] = {"--rom", "CT_ROM", "remembered"};
        if (!check_rom(rom, why, sizeof why)) {
            found = 1;
            fprintf(stderr, "ct_sdl: ROM (%s): %s\n", from[src], rom);
        } else {
            fprintf(stderr, "ct_sdl: ROM (%s): %s\n", from[src], why);
            if (!problem[0])
                snprintf(problem, sizeof problem, "%s", why);
        }
    }
    scan_dirs_init();   /* port: then the usual folders */
    if (!found && !(o->build_only && o->rom_arg)) {
        char headered[PATHLEN];
        if (!scan_for_rom(rom, sizeof rom, headered, sizeof headered)) {
            found = 1;
            fprintf(stderr, "ct_sdl: ROM (found): %s\n", rom);
        } else if (headered[0] && !problem[0]) {
            snprintf(problem, sizeof problem, "%s has a 512-byte copier header: remove it.",
                     base_name(headered));
        }
    }
    if (!found && o->build_only) {
        fprintf(stderr, "ct_sdl: no usable ROM%s\n",
                o->rom_arg ? "" : ": give one with --rom FILE");
        close_pads();
        return FIRSTRUN_ERROR;
    }
    if (!found) {
        if (wait_for_drop(rom, sizeof rom, problem[0] ? problem : NULL)) {
            close_pads();
            return FIRSTRUN_QUIT;
        }
        fprintf(stderr, "ct_sdl: ROM (dropped): %s\n", rom);
    }
    remember_rom(rom);
    bus_init(rom);
    if (!o->want_module) {
        close_pads();
        return FIRSTRUN_INTERP;
    }

    /* The module: reuse it if fresh, else build it; a fresh one that won't
       load is rebuilt once. */
    char module[PATHLEN];
#ifdef _WIN32
    pref_file(module, sizeof module, "ct_game.dll");   /* tools/build_module.py's name on Windows */
#else
    pref_file(module, sizeof module, "ct_game.so");
#endif
    int result = FIRSTRUN_MODULE;
    for (int attempt = 0;; attempt++) {
        int fresh = module_fresh(module);
        fprintf(stderr, "ct_sdl: game module %s: %s\n", module,
                fresh ? "up to date" : access(module, F_OK) ? "missing" : "stale");
        if (!fresh) {
            int b = build(rom);
            if (b == FIRSTRUN_QUIT) {
                result = FIRSTRUN_QUIT;
                break;
            }
            if (b) {
                result = offer_interp("The game code could not be built.");
                if (result == OFFER_RETRY) {
                    result = FIRSTRUN_MODULE;
                    attempt = -1;
                    continue;
                }
                break;
            }
            stamp_write(1);
        }
        scr_clear("Starting...", C_GOOD);
        for (int k = 0; k < n_steps; k++)
            scr_add(C_DIM, "  %s", steps[k]);
        scr_show(!fresh);
        const char *err;
        if (!ct_module_load(module, &err)) {
            fprintf(stderr, "ct_sdl: game module %s: %u functions\n", module, ct_func_count);
            break;
        }
        fprintf(stderr, "ct_sdl: game module: %s\n", err);
        stamp_write(0);
        if (!fresh || attempt) {
            char line[512];
            snprintf(line, sizeof line, "%s", err);
            n_tail = 0;
            on_build_line(line);
            result = offer_interp("The game code would not load.");
            if (result == OFFER_RETRY) {
                result = FIRSTRUN_MODULE;
                attempt = -1;
                continue;
            }
            break;
        }
    }
    close_pads();
    return result;
}
