/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * ntdlx.dll: stands in for ntdll.dll in a copy of zig.exe under Wine only
 * (patch-zig.py). Everything is forwarded to ntdll except:
 *  - NtQueryObject: for an ObjectNameInformation answer of the form
 *    "\??\C:\dir\file" (Wine's spelling) it returns
 *    "\Device\Mup\?\C:\dir\file", which zig's GetFinalPathNameByHandle turns
 *    into the path "\\?\C:\dir\file";
 *  - NtLockFile and NtUnlockFile: Wine refuses them (STATUS_NOT_IMPLEMENTED)
 *    when the IO_STATUS_BLOCK is given, which zig's build cache always does,
 *    and waits forever when it is not and zig's own processes hold locks on
 *    each other; here every lock is simply granted. */
#include <windows.h>
#include <winternl.h>
#include <string.h>

typedef LONG (NTAPI *query_fn)(HANDLE, int, PVOID, ULONG, PULONG);

LONG NTAPI shim_NtQueryObject(HANDLE h, int cls, PVOID info, ULONG len, PULONG ret)
{
    static query_fn real;
    if (!real)
        real = (query_fn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryObject");
    LONG st = real(h, cls, info, len, ret);
    if (st < 0 || cls != 1)   /* ObjectNameInformation */
        return st;
    UNICODE_STRING *u = info;
    static const WCHAR dos[] = L"\\??\\";
    static const WCHAR mup[] = L"\\Device\\Mup\\?\\";
    size_t n = u->Length / sizeof(WCHAR), d = 4, m = 14;
    if (n > d && !memcmp(u->Buffer, dos, d * sizeof(WCHAR))) {
        size_t total = n - d + m;
        if ((total + 1) * sizeof(WCHAR) + sizeof(UNICODE_STRING) > len)
            return (LONG)0xC0000004;   /* STATUS_INFO_LENGTH_MISMATCH */
        memmove(u->Buffer + m, u->Buffer + d, (n - d) * sizeof(WCHAR));
        memcpy(u->Buffer, mup, m * sizeof(WCHAR));
        u->Length = (USHORT)(total * sizeof(WCHAR));
        u->Buffer[total] = 0;
        if (ret)
            *ret = (ULONG)(sizeof(UNICODE_STRING) + (total + 1) * sizeof(WCHAR));
    }
    return st;
}

LONG NTAPI shim_NtLockFile(HANDLE h, HANDLE ev, PVOID apc, PVOID ctx, PIO_STATUS_BLOCK io,
                           PLARGE_INTEGER off, PLARGE_INTEGER cnt, PULONG key, BOOLEAN fail_now,
                           BOOLEAN excl)
{
    (void)h; (void)ev; (void)apc; (void)ctx; (void)off; (void)cnt; (void)key; (void)fail_now;
    (void)excl;
    if (io) {
        io->Status = 0;
        io->Information = 0;
    }
    return 0;   /* granted */
}

LONG NTAPI shim_NtUnlockFile(HANDLE h, PIO_STATUS_BLOCK io, PLARGE_INTEGER off,
                             PLARGE_INTEGER cnt, PULONG key)
{
    (void)h; (void)off; (void)cnt; (void)key;
    if (io) {
        io->Status = 0;
        io->Information = 0;
    }
    return 0;
}
