#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Before/after pictures and GPU timings of the HD-2D renderer.

usage: hd2d_shots.py --label NAME [--build DIR] [--sizes 1038x556,1920x1080]
                     [--presets off,hd2d,hd2d3d,deck3d] [--only FRAME,...]
                     [--sheet-size 1038x556] [--dry-run] [--list]

It builds nothing: it runs the existing ct_sdl_engine with the game module
(offscreen EGL, private XDG dirs, muted, no saves) once per replay, preset
and size, with one --shot per wanted frame, and CT_HD2D_PROFILE for the
timings. Output, all under BUILD/qa/NAME/ (ROM-derived: never committed):

  <preset>_<size>/<frame>.png    the shots (frame zero-padded to 5 digits)
  <preset>_<size>/<replay>.prof  the renderer's per-frame profile file
  sheet_<frame>.png              the presets side by side, at --sheet-size
  timings.txt                    median GPU ms per pass, per preset, per size

The timings come from the box's GPU, which is not the ROG Ally's: compare
presets and before/after runs on the same machine, not absolute numbers.
The frame list is FRAMES below; the renderer knows nothing about it.
"""
import argparse
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
REPLAY_DIR = os.path.join(ROOT, 'game', 'ct', 'test', 'replay')

PRESETS = ('off', 'hd2d', 'hd2d3d', 'deck3d')
SIZES = ('1038x556', '1920x1080')

# (replay, frame, what).  The frames are 1-based, as the replays' own.
FRAMES = (
    ('leene_square', 3000, 'opening flyover'),
    ('leene_square', 3850, 'narrator on black'),
    ('leene_square', 4300, 'Crono\'s room, Mom\'s text box'),
    ('leene_square', 4690, 'Crono\'s room'),
    ('leene_square', 5950, 'the house'),
    ('leene_square', 8700, 'Leene Square'),
    ('leene_square', 9200, 'Leene Square, the bell plaza'),
    ('gato_win', 11000, 'battle (Gato)'),
    ('menu_tour', 9400, 'field menu'),
    ('world_map_save', 7600, 'world map'),
)


def parse_size(s):
    w, h = s.lower().split('x')
    return int(w), int(h)


def frame_tag(frame):
    return '%05d' % frame


def group_by_replay(frames):
    """-> {replay: [frame, ...]} in list order, frames sorted per replay."""
    out = {}
    for replay, frame, _ in frames:
        out.setdefault(replay, []).append(frame)
    return {r: sorted(set(f)) for r, f in out.items()}


def shot_path(outdir, preset, size, frame):
    return os.path.join(outdir, '%s_%s' % (preset, size), frame_tag(frame) + '.png')


def prof_path(outdir, preset, size, replay):
    return os.path.join(outdir, '%s_%s' % (preset, size), replay + '.prof')


def build_command(engine, module, replay, frames, preset, size, outdir):
    """The argument list for one run: one replay, one preset, one size."""
    cmd = [engine, '--module', module,
           '--script', os.path.join(REPLAY_DIR, replay + '.txt'),
           '--frames', str(max(frames) + 1),
           '--fast', '--no-save', '--mute', '--hd2d', '--preset', preset,
           '--shot-size', size]
    for f in frames:
        cmd += ['--shot', '%d:%s' % (f, shot_path(outdir, preset, size, f))]
    return cmd


def run_env(workdir, profile):
    """Offscreen EGL, muted, private XDG dirs (nothing of the user's is read or written)."""
    return dict(os.environ, SDL_VIDEODRIVER='offscreen', SDL_AUDIODRIVER='dummy',
                SDL_JOYSTICK_HIDAPI='0', CT_HD2D_PROFILE=profile,
                XDG_DATA_HOME=os.path.join(workdir, 'data'),
                XDG_CONFIG_HOME=os.path.join(workdir, 'config'))


def parse_profile(text):
    """Profile lines 'frame=N total=.. gpu=.. pass=..' -> {frame: {name: ms}}."""
    out = {}
    for line in text.splitlines():
        row = {}
        for tok in line.split():
            k, _, v = tok.partition('=')
            try:
                row[k] = float(v)
            except ValueError:
                row = {}
                break
        if 'frame' in row:
            out[int(row.pop('frame'))] = row
    return out


def median_table(profiles):
    """profiles: list of {name: ms} rows -> {name: median} (only names in at least one row)."""
    names = []
    for r in profiles:
        for k in r:
            if k not in names:
                names.append(k)
    return {n: statistics.median([r[n] for r in profiles if n in r]) for n in names}


def format_table(title, cols):
    """cols: {preset: {pass: ms}} -> text, one row per pass."""
    names = []
    for c in cols.values():
        for k in c:
            if k not in names:
                names.append(k)
    lead = [n for n in ('gpu', 'analyze', 'upload', 'submit') if n in names]
    names = lead + [n for n in names if n not in lead and n != 'total']
    head = '%-12s' % title + ''.join('%10s' % p for p in cols)
    lines = [head]
    for n in names:
        lines.append('%-12s' % n + ''.join(
            '%10s' % ('%.3f' % c[n] if n in c else '-') for c in cols.values()))
    return '\n'.join(lines)


def make_sheet(outdir, frame, presets, size, path):
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    from pack_upscale import png_read, png_write
    imgs = [png_read(shot_path(outdir, p, size, frame)) for p in presets]
    w, h = imgs[0][0], imgs[0][1]
    gap = 4
    sw = w * len(imgs) + gap * (len(imgs) - 1)
    sheet = bytearray(b'\x20\x20\x20\xff' * (sw * h))
    for i, (iw, ih, px) in enumerate(imgs):
        if (iw, ih) != (w, h):
            raise SystemExit('hd2d_shots: shot sizes differ in the sheet for frame %d' % frame)
        x0 = i * (w + gap)
        for y in range(h):
            o = (y * sw + x0) * 4
            sheet[o:o + w * 4] = px[y * w * 4:(y + 1) * w * 4]
    png_write(path, sw, h, sheet)


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--label', help='name of the run: output goes to BUILD/qa/LABEL/')
    ap.add_argument('--build', default=os.path.join(ROOT, 'build'))
    ap.add_argument('--presets', default=','.join(PRESETS))
    ap.add_argument('--sizes', default=','.join(SIZES))
    ap.add_argument('--sheet-size', default=SIZES[0])
    ap.add_argument('--only', help='comma-separated frames to take (default: all)')
    ap.add_argument('--dry-run', action='store_true', help='print the command lines, run nothing')
    ap.add_argument('--list', action='store_true', help='print the frame list and exit')
    a = ap.parse_args(argv)

    frames = list(FRAMES)
    if a.only:
        want = {int(x) for x in a.only.split(',')}
        frames = [f for f in frames if f[1] in want]
    if a.list:
        for r, f, what in frames:
            print('%-15s %6d  %s' % (r, f, what))
        return 0
    if not a.label:
        ap.error('--label is required')
    presets = a.presets.split(',')
    sizes = a.sizes.split(',')
    for s in sizes + [a.sheet_size]:
        parse_size(s)
    engine = os.path.join(a.build, 'ct_sdl_engine')
    module = os.path.join(a.build, 'ct_game.so')
    outdir = os.path.join(a.build, 'qa', a.label)
    work = os.path.join(outdir, '_work')
    groups = group_by_replay(frames)

    for size in sizes:
        for preset in presets:
            for replay, fl in groups.items():
                cmd = build_command(engine, module, replay, fl, preset, size, outdir)
                if a.dry_run:
                    print(' '.join(cmd))
                    continue
                os.makedirs(os.path.dirname(shot_path(outdir, preset, size, fl[0])), exist_ok=True)
                prof = prof_path(outdir, preset, size, replay)
                os.makedirs(work, exist_ok=True)
                r = subprocess.run(cmd, env=run_env(work, prof), capture_output=True, text=True,
                                   timeout=1800)
                log = r.stdout + r.stderr
                if 'no usable OpenGL' in log or 'cannot create an OpenGL' in log:
                    print('hd2d_shots: no OpenGL here (offscreen EGL failed):\n' + log[-1500:])
                    return 3
                missing = [f for f in fl if not os.path.exists(shot_path(outdir, preset, size, f))]
                if r.returncode != 0 or missing:
                    print(log[-2000:])
                    print('hd2d_shots: %s %s %s failed (exit %d, missing %s)'
                          % (replay, preset, size, r.returncode, missing))
                    return 1
                print('ok  %-15s %-7s %s  %d shots' % (replay, preset, size, len(fl)))
    if a.dry_run:
        return 0

    if a.sheet_size in sizes and len(presets) > 1:
        for _, f, _ in frames:
            make_sheet(outdir, f, presets, a.sheet_size, os.path.join(outdir, 'sheet_%s.png' % frame_tag(f)))
        print('sheets: %s/sheet_*.png' % outdir)

    text = ['GPU ms per pass, median over %d frames (box GPU, not the ROG Ally)' % len(frames)]
    for size in sizes:
        cols = {}
        for preset in presets:
            rows = []
            for replay, fl in groups.items():
                p = prof_path(outdir, preset, size, replay)
                prof = parse_profile(open(p).read()) if os.path.exists(p) else {}
                rows += [prof[f] for f in fl if f in prof]
            cols[preset] = median_table(rows) if rows else {}
        text += ['', format_table(size, cols)]
    table = '\n'.join(text)
    with open(os.path.join(outdir, 'timings.txt'), 'w') as f:
        f.write(table + '\n')
    print(table)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
