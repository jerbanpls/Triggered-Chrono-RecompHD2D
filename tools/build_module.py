#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build a game module (runtime/module.h) from the user's own ROM, without
CMake: what an installed engine runs on first launch.

  1. check the ROM against GAME/game.toml (tools/check_rom.py)
  2. decode the game's WRAM overlays (GAME/tools/overlay_images.py)
  3. translate to C (recomp/emit.py)
  4. compile every file in parallel and link OUT/ct_game.so
     (OUT/ct_game.dll on Windows, against the engine's import library)

usage: build_module.py --game DIR --rom FILE --out DIR [--cc CC]
                       [--native] [--jobs N] [--keep-c] [--engine-lib FILE]
       build_module.py --synth DIR --out DIR [--cc CC] [--engine-lib FILE]

--cc may be a command with arguments ("zig cc -target x86_64-windows-gnu").
--synth builds the module from the C files in DIR instead of a game (no ROM,
no translation): the synthetic module the Windows package tests use.
--engine-lib (Windows): the import library of the engine program that will
load the module; default libct_sdl_engine.dll.a next to tools/.

--native compiles for this machine's CPU (-march=native); the module is
never copied anywhere else, so it may as well be. The generated C is
deleted after a successful build unless --keep-c: it is derived from the
ROM and has no use once compiled.
"""
from __future__ import annotations

import argparse
import concurrent.futures as cf
import os
import shlex
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WINDOWS = sys.platform == 'win32'


def split_cc(cc: str) -> list[str]:
    """--cc as an argv prefix: an existing file is one word (a path with
    spaces), anything else is split like a shell would (Windows: quotes are
    honoured, backslashes are not escapes)."""
    if os.path.exists(cc):
        return [cc]
    words = shlex.split(cc, posix=not WINDOWS)
    if WINDOWS:
        words = [w[1:-1] if len(w) > 1 and w[0] == w[-1] and w[0] in '"\'' else w for w in words]
    return words


def step(msg: str) -> None:
    print(f'build_module: {msg}', flush=True)


def run(cmd: list[str], env: dict | None = None, capture: bool = False) -> str:
    r = subprocess.run(cmd, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'build_module: failed: {" ".join(cmd)}')
    return r.stdout if capture else ''


# One compile of a big generated file takes up to about 600 MB. On a handheld
# with 16 cores, one job per core can run the machine out of memory (the
# kernel then kills the game or the desktop), so fit the jobs to the memory
# that is free now, keeping 1 GB spare.
JOB_MB = 600


def avail_mb_windows() -> int:
    import ctypes

    class MemStatus(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong),
                    ('total_phys', ctypes.c_ulonglong), ('avail_phys', ctypes.c_ulonglong),
                    ('total_page', ctypes.c_ulonglong), ('avail_page', ctypes.c_ulonglong),
                    ('total_virt', ctypes.c_ulonglong), ('avail_virt', ctypes.c_ulonglong),
                    ('avail_ext', ctypes.c_ulonglong)]

    st = MemStatus()
    st.length = ctypes.sizeof(st)
    if not ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(st)):
        raise OSError('GlobalMemoryStatusEx')
    return st.avail_phys // (1024 * 1024)


def default_jobs() -> int:
    cpus = os.cpu_count() or 4
    if WINDOWS:
        try:
            return max(1, min(cpus, (avail_mb_windows() - 1024) // JOB_MB))
        except (OSError, AttributeError):
            return cpus
    try:
        with open('/proc/meminfo') as f:
            avail_kb = next(int(l.split()[1]) for l in f if l.startswith('MemAvailable:'))
    except (OSError, StopIteration, ValueError):
        return cpus
    return max(1, min(cpus, (avail_kb // 1024 - 1024) // JOB_MB))


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--game', help='game directory, e.g. game/<id>')
    ap.add_argument('--rom')
    ap.add_argument('--synth', metavar='DIR', help='build the C files in DIR as a module (no game)')
    ap.add_argument('--engine-lib', help='Windows: import library of the engine program')
    ap.add_argument('--out', required=True)
    ap.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    ap.add_argument('--native', action='store_true')
    ap.add_argument('--jobs', type=int, default=default_jobs())
    ap.add_argument('--keep-c', action='store_true')
    a = ap.parse_args(argv[1:])
    if not a.synth and not (a.game and a.rom):
        ap.error('--game and --rom are required (or --synth DIR)')
    cc = split_cc(a.cc)

    game = os.path.abspath(a.game) if a.game else ''
    rom = os.path.abspath(a.rom) if a.rom else ''
    out = os.path.abspath(a.out)
    gen = os.path.join(out, 'gen')
    objs = os.path.join(out, 'obj')
    os.makedirs(gen, exist_ok=True)
    os.makedirs(objs, exist_ok=True)
    py = sys.executable
    env = dict(os.environ, CT_ROM=rom)
    t0 = time.time()

    if a.synth:
        sources = sorted(os.path.join(a.synth, f) for f in os.listdir(a.synth) if f.endswith('.c'))
        sources.append(os.path.join(ROOT, 'runtime', 'module_entry.c'))
    else:
        step('checking the ROM')
        run([py, os.path.join(ROOT, 'tools', 'check_rom.py'), game, rom])

        step('decoding overlays')
        # (the overlay tool writes this folder's paths into a TOML string: on
        # Windows they need forward slashes, and a closing one so its joins
        # use them too)
        ovdir = os.path.join(gen, 'overlays')
        if WINDOWS:
            ovdir = ovdir.replace('\\', '/') + '/'
        run([py, os.path.join(game, 'tools', 'overlay_images.py'), os.path.join(game, 'overlays.toml'),
             ovdir], env)

        step('translating to C')
        funcs = os.path.join(game, 'funcs.toml')
        run([py, os.path.join(ROOT, 'recomp', 'emit.py'), '--out', gen, '--funcs', funcs,
             '--overlays', os.path.join(gen, 'overlays', 'manifest.toml')], env)
        modules = run([py, os.path.join(ROOT, 'recomp', 'emit.py'), '--list-modules', '--funcs', funcs],
                      capture=True).replace(';', ' ').split()   # a CMake list

        sources = [os.path.join(gen, f'{m}.c') for m in modules]
        # The game's own C (its extern hooks, its scene description): every .c
        # at the top of its directory.
        game_c = sorted(os.path.join(game, f) for f in os.listdir(game) if f.endswith('.c'))
        sources += [os.path.join(gen, 'ct_overlays.c'), os.path.join(gen, 'ct_funcs.c'), *game_c,
                    os.path.join(ROOT, 'runtime', 'module_entry.c')]

    cflags = ['-O2', *([] if WINDOWS else ['-fPIC', '-fvisibility=hidden']), '-std=c11',
              '-DCT_TEST_BUILD', '-DCT_TABLES_DEFINE',
              *([] if a.synth else ['-I', gen]), '-I', os.path.join(ROOT, 'runtime'),
              *([] if a.synth else ['-I', game])]   # the game's own headers (e.g. game/<id>/*.h)
    if a.native:
        cflags.append('-march=native')

    step(f'compiling {len(sources)} files with {a.cc} ({a.jobs} jobs)')

    def compile_one(src: str) -> str:
        obj = os.path.join(objs, os.path.basename(src) + '.o')
        run([*cc, *cflags, '-c', src, '-o', obj])
        return obj

    with cf.ThreadPoolExecutor(max_workers=a.jobs) as ex:
        # Biggest first: the long poles start early.
        order = sorted(sources, key=lambda s: -os.path.getsize(s))
        obj_files = list(ex.map(compile_one, order))

    stem = 'ct_synth' if a.synth else 'ct_game'
    module = os.path.join(out, stem + ('.dll' if WINDOWS else '.so'))
    step('linking')
    if WINDOWS:
        # the module takes the engine's symbols from the engine program's
        # import library (a PE file binds imports by the program's name)
        lib = a.engine_lib or os.path.join(ROOT, 'libct_sdl_engine.dll.a')
        if not os.path.isfile(lib):
            raise SystemExit(f'build_module: engine import library not found: {lib} (--engine-lib)')
        run([*cc, '-shared', '-o', module + '.tmp', *obj_files, lib])
    else:
        run([*cc, '-shared', '-o', module + '.tmp', *obj_files])
    os.replace(module + '.tmp', module)
    if WINDOWS:   # the linker's debug file and import library of the module itself
        for f in os.listdir(out):
            if f.endswith(('.pdb', '.lib')):
                os.remove(os.path.join(out, f))
    if not a.keep_c:
        shutil.rmtree(gen)
    shutil.rmtree(objs)
    step(f'done: {module} ({os.path.getsize(module) // 1024} KiB, {time.time() - t0:.0f} s)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
