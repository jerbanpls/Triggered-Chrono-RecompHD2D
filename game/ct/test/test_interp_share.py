#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Interpreter-hit counter test (upstream issue #108).

usage: test_interp_share.py CT_BOOT REPLAY_DIR CEILINGS NAME

Runs replay NAME headless with ct_boot --profile 0 and fails if the share of
instructions run in the interpreter is above the ceiling recorded for it in
CEILINGS (interp_ceilings.txt). Ceilings are only ever lowered.
"""
import re
import subprocess
import sys


def main() -> int:
    if len(sys.argv) != 5:
        print(__doc__, file=sys.stderr)
        return 2
    boot, rdir, table, name = sys.argv[1:]
    for line in open(table):
        f = line.split()
        if f and not f[0].startswith('#') and f[0] == name:
            break
    else:
        print(f'test_interp_share: no ceiling for {name}')
        return 1
    frames, ceiling, extra = f[1], float(f[2]), f[3:]
    r = subprocess.run([boot, '--frames', frames, '--script', f'{rdir}/{name}.txt',
                        *extra, '--profile', '0'], capture_output=True, text=True)
    if r.returncode:
        print(f'test_interp_share: {name}: ct_boot exit {r.returncode}: {r.stdout[-300:]}')
        return 1
    m = re.search(r'profile: (\d+) instructions, .*\((\d+) native, (\d+) interpreted\)', r.stdout)
    if not m:
        print(f'test_interp_share: {name}: no profile line')
        return 1
    total, interp = int(m.group(1)), int(m.group(3))
    share = 100.0 * interp / total
    print(f'test_interp_share: {name}: {share:.3f}% interpreted (ceiling {ceiling}%)')
    if share > ceiling:
        print(f'test_interp_share: {name}: interpreter share rose above its ceiling')
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
