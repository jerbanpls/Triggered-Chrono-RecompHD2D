#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine-only workaround for zig.exe (EXPERIMENTAL Windows package tests).

zig finds its own path with NtQueryObject(ObjectNameInformation) and expects
a "\\Device\\HarddiskVolumeN\\..." name; Wine answers "\\??\\C:\\...", so every
`zig cc` fails with "unable to find zig self exe path: Unexpected".

  patch-zig.py ZIG.EXE OUTDIR

writes OUTDIR/zig.exe, a copy whose import of ntdll.dll is renamed to
ntdlx.dll (same length), and OUTDIR/ntdlx.def, which forwards every other
ntdll function zig imports to the real ntdll. ntdlx.dll (shim.c, built by
test-wine.sh) answers NtQueryObject, NtLockFile and NtUnlockFile itself (locks are always granted). The package's own zig.exe is
never modified, and none of this runs on real Windows.
"""
import struct
import sys

src, outdir = sys.argv[1:3]
SHIMMED = ('NtQueryObject', 'NtLockFile', 'NtUnlockFile')   # answered by shim.c
data = bytearray(open(src, 'rb').read())
pe = struct.unpack_from('<I', data, 0x3C)[0]
assert data[pe:pe + 4] == b'PE\0\0'
nsec, = struct.unpack_from('<H', data, pe + 6)
optsz, = struct.unpack_from('<H', data, pe + 20)
opt = pe + 24
assert struct.unpack_from('<H', data, opt)[0] == 0x20B   # PE32+
imp_rva, = struct.unpack_from('<I', data, opt + 112 + 8)
secs = []
for i in range(nsec):
    o = opt + optsz + i * 40
    vsize, va, rawsz, raw = struct.unpack_from('<IIII', data, o + 8)
    secs.append((va, max(vsize, rawsz), raw))


def off(rva):
    for va, sz, raw in secs:
        if va <= rva < va + sz:
            return rva - va + raw
    raise SystemExit(f'patch-zig: RVA {rva:#x} not in a section')


def cstr(o):
    e = data.index(b'\0', o)
    return bytes(data[o:e]).decode('latin1')


names = []
patched = False
d = off(imp_rva)
while True:
    oft, _, _, name_rva, ft = struct.unpack_from('<IIIII', data, d)
    if not name_rva:
        break
    if cstr(off(name_rva)).lower() == 'ntdll.dll':
        no = off(name_rva)
        data[no:no + 9] = b'ntdlx.dll'
        patched = True
        t = off(oft or ft)
        while True:
            v, = struct.unpack_from('<Q', data, t)
            if not v:
                break
            if v >> 63:
                raise SystemExit('patch-zig: import by ordinal from ntdll.dll')
            names.append(cstr(off(v & 0x7FFFFFFF) + 2))
            t += 8
    d += 20
if not patched:
    raise SystemExit('patch-zig: no import of ntdll.dll')
open(f'{outdir}/zig.exe', 'wb').write(data)
with open(f'{outdir}/ntdlx.def', 'w') as f:
    f.write('LIBRARY ntdlx.dll\nEXPORTS\n')
    for n in sorted(set(names)):
        if n in SHIMMED:
            f.write(f'    {n} = shim_{n}\n')
        else:
            f.write(f'    {n} = ntdll.{n}\n')
print(f'patch-zig: {len(set(names))} ntdll imports')
