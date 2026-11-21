/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * cmd.exe stand-in for the Wine ctest run (packaging/windows/test-wine.sh):
 * "cmd /c LINE" becomes "C:\bb\sh.exe -c LINE" (mounted over
 * C:\windows\system32\cmd.exe with WINEDLLOVERRIDES=cmd.exe=n). system() and
 * popen() run the command line through cmd.exe /c, and the tests that use them write
 * POSIX shell (single quotes, pipes); on the Windows CI that works because
 * MSYS2 programs parse single quotes themselves, busybox-w32's do not. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

int main(void)
{
    const char *cl = GetCommandLineA();
    /* skip the program name (quoted or not), then " /c " */
    if (*cl == '"') {
        cl = strchr(cl + 1, '"');
        cl = cl ? cl + 1 : "";
    } else {
        while (*cl && *cl != ' ')
            cl++;
    }
    while (*cl == ' ')
        cl++;
    if ((cl[0] != '/' && cl[0] != '-') || (cl[1] != 'c' && cl[1] != 'C')) {
        fprintf(stderr, "shim-cmd: only 'cmd /c LINE' is supported\n");
        return 2;
    }
    cl += 2;
    while (*cl == ' ')
        cl++;
    size_t n = strlen(cl);
    char *copy = strdup(cl);
    if (n >= 2 && copy[0] == '"' && copy[n - 1] == '"') {   /* cmd /c "LINE": cmd drops the outer quotes */
        copy[n - 1] = 0;
        cl = copy + 1;
        n -= 2;
    }
    char *line = malloc(n * 2 + 64), *o = line;
    o += sprintf(o, "C:\\bb\\sh.exe -c \"");
    for (const char *p = cl; *p; p++) {
        size_t bs = 0;
        while (*p == '\\') { bs++; p++; }
        if (!*p) { while (bs--) { *o++ = '\\'; *o++ = '\\'; } break; }
        if (*p == '"') { while (bs--) { *o++ = '\\'; *o++ = '\\'; } *o++ = '\\'; *o++ = '"'; }
        else { while (bs--) *o++ = '\\'; *o++ = *p; }
    }
    *o++ = '"';
    *o = 0;
    STARTUPINFOA si = {sizeof si};
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "shim-cmd: cannot start sh.exe\n");
        return 127;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD rc = 1;
    GetExitCodeProcess(pi.hProcess, &rc);
    return (int)rc;
}
