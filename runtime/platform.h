/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * port: the few places POSIX and 64-bit Windows (MinGW, UCRT) differ for
 * the engine and its frontend. On everything but Windows this header adds
 * nothing but the ct_dl* wrappers over dlopen, so those builds behave as
 * before. Include it AFTER the system headers: on Windows it renames some
 * POSIX calls to the functions below.
 *
 *   ct_dlopen / ct_dlsym / ct_dlclose / ct_dlerror: shared libraries
 *     (module.c loads the game module, qol.c libdbus). The Windows
 *     versions are LoadLibraryW (the path is UTF-8), GetProcAddress and
 *     FreeLibrary; an unresolved name is NULL, as with dlsym.
 *   ct_home: the user's home folder (HOME, else USERPROFILE on Windows).
 */
#ifndef CT_PLATFORM_H
#define CT_PLATFORM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *ct_dlopen(const char *path);
void *ct_dlsym(void *lib, const char *name);
void ct_dlclose(void *lib);
const char *ct_dlerror(void);   /* the last failure, for a message; never NULL */

const char *ct_home(void);      /* NULL when unknown */
const char *ct_tmpdir(void);    /* for tests: $TMPDIR or /tmp; on Windows %TEMP% */

#ifdef _WIN32
int ct_rename(const char *from, const char *to);   /* replaces an existing file, like rename(2) */
int ct_link(const char *existing, const char *name);   /* a hard link; -1 if the volume has none */
char *ct_mkdtemp(char *tmpl);   /* like mkdtemp(3): "...XXXXXX" becomes a new directory */
#endif

#ifdef __cplusplus
}
#endif

#ifndef O_BINARY
#define O_BINARY 0   /* POSIX has no text mode; Windows open() has, and it mangles binary files */
#endif

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <time.h>

/* mkdir takes no mode; fsync is _commit; rename must replace; no symlinks
   (lstat is stat); realpath is _fullpath; setenv is _putenv_s. */
#define mkdir(path, mode) _mkdir(path)
#define fsync(fd) _commit(fd)
#define rename(from, to) ct_rename((from), (to))
#define link(existing, name) ct_link((existing), (name))
#define mkdtemp(tmpl) ct_mkdtemp((tmpl))
#define lstat(path, st) stat((path), (st))
#define realpath(path, out) _fullpath((out), (path), 4096)
#define setenv(name, value, overwrite) _putenv_s((name), (value))
#define unsetenv(name) _putenv_s((name), "")
#define localtime_r(t, tm) (localtime_s((tm), (t)) ? NULL : (tm))
#endif

#endif
