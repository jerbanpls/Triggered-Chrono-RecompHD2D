# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""hd2d_shots.py's frame list and command lines (no ROM, no renderer run).

usage: test_hd2d_shots.py
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location(
    'hd2d_shots', os.path.join(os.path.dirname(HERE), 'tools', 'hd2d_shots.py'))
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

bad = 0


def check(ok, what):
    global bad
    print(('ok   ' if ok else 'FAIL ') + what)
    bad += 0 if ok else 1


# every replay exists and every frame is positive, unique per replay
reps = {r for r, _, _ in m.FRAMES}
check(all(os.path.exists(os.path.join(m.REPLAY_DIR, r + '.txt')) for r in reps), 'replays exist')
check(len(set((r, f) for r, f, _ in m.FRAMES)) == len(m.FRAMES), 'frames unique')
want = {3000, 3850, 4300, 4690, 5950, 8700, 9200}
check(want <= {f for r, f, _ in m.FRAMES if r == 'leene_square'}, 'required Leene Square frames')
check({'gato_win', 'menu_tour', 'world_map_save'} <= reps, 'battle, menu, world map present')

# frames lie inside each replay's length (last input line's frame is a lower bound only)
g = m.group_by_replay(m.FRAMES)
check(g['leene_square'] == sorted(g['leene_square']), 'frames sorted per replay')

cmd = m.build_command('E', 'M.so', 'leene_square', g['leene_square'], 'hd2d3d', '1038x556', '/o')
check(cmd[0] == 'E' and cmd[cmd.index('--module') + 1] == 'M.so', 'engine and module')
check(cmd[cmd.index('--frames') + 1] == str(max(g['leene_square']) + 1), 'frames count')
check(cmd[cmd.index('--preset') + 1] == 'hd2d3d' and cmd[cmd.index('--shot-size') + 1] == '1038x556', 'preset, size')
check('--mute' in cmd and '--no-save' in cmd, 'muted, no saves')
shots = [cmd[i + 1] for i, c in enumerate(cmd) if c == '--shot']
check(len(shots) == len(g['leene_square']) and '9200:/o/hd2d3d_1038x556/09200.png' in shots, 'shot args')
check(cmd[cmd.index('--script') + 1].endswith('replay/leene_square.txt'), 'script path')

# profile parsing and the table
p = m.parse_profile('frame=5 total=1.0 gpu=2.0 light=0.5\nframe=6 total=1.0 gpu=4.0 light=1.5\nbad line=x\n')
check(sorted(p) == [5, 6] and p[6]['gpu'] == 4.0, 'profile parse')
t = m.median_table(list(p.values()))
check(t['gpu'] == 3.0 and t['light'] == 1.0, 'median')
check('light' in m.format_table('s', {'a': t}), 'table')
sys.exit(1 if bad else 0)
