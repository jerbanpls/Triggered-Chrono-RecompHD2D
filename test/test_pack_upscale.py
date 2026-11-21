#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/pack_upscale.py on made-up pictures (no game data): PNG round
trip, the scaler (sizes, flat areas and transparency kept, a diagonal
smoothed), a whole pack upscaled to a new directory, and --external
with an "upscaler" that loses the alpha channel."""
import os
import shlex
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.join(HERE, '..', 'tools')
sys.path.insert(0, TOOLS)
import pack_upscale as pu  # noqa: E402

fails = 0


def check(ok, msg):
    global fails
    if not ok:
        fails += 1
        print('FAIL:', msg)


def picture():
    """12x12: transparent background, a red block in the top-left, a
    blue diagonal staircase."""
    w = h = 12
    rgba = bytearray(w * h * 4)
    for y in range(h):
        for x in range(w):
            o = (y * w + x) * 4
            if x < 4 and y < 4:
                rgba[o:o + 4] = b'\xff\x20\x20\xff'
            elif x >= 4 and y >= 4 and x - 4 <= y - 4:
                rgba[o:o + 4] = b'\x20\x20\xff\xff'
    return w, h, rgba


def main():
    w, h, rgba = picture()
    with tempfile.TemporaryDirectory() as td:
        p = os.path.join(td, 'a.png')
        pu.png_write(p, w, h, rgba)
        check(pu.png_read(p) == (w, h, rgba), 'PNG round trip')
        for s in range(2, 7):
            W, H, out = pu.upscale(w, h, rgba, s)
            check((W, H) == (w * s, h * s), f'{s}x size {W}x{H}')

            def px(x, y):
                return bytes(out[(y * W + x) * 4:(y * W + x) * 4 + 4])
            check(px(s, s) == b'\xff\x20\x20\xff', f'{s}x: flat red kept')
            check(px(W - 1, 0) == b'\0\0\0\0', f'{s}x: transparent corner kept')
            # The staircase's steps are cut: some output pixel inside a
            # step's source pixel takes the other side's value.
            changed = 0
            for y in range(4, 12):
                for x in range(4, 12):
                    for oy in range(s):
                        for ox in range(s):
                            o = ((y * s + oy) * W + x * s + ox) * 4
                            changed += bytes(out[o:o + 4]) != bytes(rgba[(y * w + x) * 4:(y * w + x) * 4 + 4])
            check(changed > 0, f'{s}x: the diagonal is smoothed')

        # A pack: pack.toml and two pictures; --out makes a new one.
        src = os.path.join(td, 'packs', 'p')
        os.makedirs(os.path.join(src, 'tiles'))
        pu.png_write(os.path.join(src, 'tiles', '0000000000000001.png'), w, h, rgba)
        pu.png_write(os.path.join(src, 'tiles', '0000000000000002.png'), 8, 8, bytes(8 * 8 * 4))
        with open(os.path.join(src, 'pack.toml'), 'w') as f:
            f.write('[pack]\nversion = 1\nname = "p"\nscale = 1\n\n'
                    '[[tile]]\nkey = "0000000000000001"\nfile = "tiles/0000000000000001.png"\nw = 12\nh = 12\n\n'
                    '[[tile]]\nkey = "0000000000000002"\nfile = "tiles/0000000000000002.png"\nw = 8\nh = 8\n')
        dst = os.path.join(td, 'packs', 'p3')
        r = subprocess.run([sys.executable, os.path.join(TOOLS, 'pack_upscale.py'), src, '--scale', '3',
                            '--out', dst, '--jobs', '1'], capture_output=True, text=True)
        check(r.returncode == 0, f'pack_upscale --out: {r.stderr}')
        man = open(os.path.join(dst, 'pack.toml')).read()
        check('scale = 3' in man and 'name = "p3"' in man and 'w = 12' in man, 'manifest: ' + man[:200])
        check(pu.png_read(os.path.join(dst, 'tiles', '0000000000000001.png'))[:2] == (36, 36), '3x picture')
        r = subprocess.run([sys.executable, os.path.join(TOOLS, 'pack_upscale.py'), dst, '--scale', '2',
                            '--in-place'], capture_output=True, text=True)
        check(r.returncode != 0 and 'already at scale' in r.stderr, 'refuses to upscale twice')

        # --external: a nearest-neighbor "upscaler" that writes RGB only.
        ext = os.path.join(td, 'ext.py')
        with open(ext, 'w') as f:
            f.write('import sys\nsys.path.insert(0, %r)\nimport pack_upscale as pu\n'
                    'w, h, a = pu.png_read(sys.argv[1]); s = int(sys.argv[3])\n'
                    'o = bytearray()\n'
                    'for y in range(h * s):\n'
                    '    for x in range(w * s):\n'
                    '        k = ((y // s) * w + x // s) * 4\n'
                    '        o += bytes(a[k:k + 3]) + b"\\xff"\n'
                    'pu.png_write(sys.argv[2], w * s, h * s, o)\n' % TOOLS)
        dst2 = os.path.join(td, 'packs', 'pe')
        r = subprocess.run([sys.executable, os.path.join(TOOLS, 'pack_upscale.py'), src, '--scale', '2',
                            '--out', dst2, '--jobs', '1', '--external',
                            f'{shlex.quote(sys.executable)} {shlex.quote(ext)} {{in}} {{out}} {{scale}}'], capture_output=True, text=True)
        check(r.returncode == 0, f'--external: {r.stderr}')
        W, H, out = pu.png_read(os.path.join(dst2, 'tiles', '0000000000000001.png'))
        check((W, H) == (24, 24), '--external size')
        check(out[(0 * W + W - 1) * 4 + 3] == 0, '--external: alpha restored where the tool lost it')
        check(out[(1 * W + 1) * 4:(1 * W + 1) * 4 + 4] == b'\xff\x20\x20\xff', '--external: its colors kept')
    print('pack_upscale:', 'ok' if not fails else f'{fails} failed')
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
