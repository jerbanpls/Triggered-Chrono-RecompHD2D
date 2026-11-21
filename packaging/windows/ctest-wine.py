#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the ROM-free ctest set of a cross-built Windows tree (EXPERIMENTAL
package tests; called by packaging/windows/test-wine.sh).

usage: ctest-wine.py OUT REPO EXPECT_N

Reads the test list from the build tree (ctest --show-only=json-v1, run in
the ct-build box, where cmake lives). Tests whose command is a Windows
program run under Wine through wine.sh (absolute paths in arguments and
environment become Z:/ paths); tests whose command is a script (Python tools
and lint) run in the box, as ctest runs them on Linux: those test Python
code, which needs no Windows. SKIP_RETURN_CODE and PASS_REGULAR_EXPRESSION
are honoured. Prints a ctest-style summary; the line "100% tests passed, 0
tests failed out of N" only when nothing failed and N is EXPECT_N.
"""
import json
import os
import re
import subprocess
import sys

TIMEOUT = 300


def box(cmd, **kw):
    return subprocess.run(['distrobox', 'enter', 'ct-build', '--'] + cmd, **kw)


def zpath(s):
    return 'Z:' + s if s.startswith('/') and not s.startswith('//') else s


def main(argv):
    out, repo, expect = os.path.abspath(argv[1]), os.path.abspath(argv[2]), int(argv[3])
    bld = os.path.join(out, 'build')
    here = os.path.dirname(os.path.abspath(__file__))
    r = box(['sh', '-c', 'cd "$1" && ctest --show-only=json-v1 -LE rom', 'sh', bld],
            capture_output=True, text=True)
    tests = json.loads(r.stdout)['tests']
    print(f'tests listed in the Windows tree: {len(tests)}; Linux engine-only build: {expect}')
    passed, failed, skipped, ran_wine, ran_box = [], [], [], 0, 0
    for i, t in enumerate(tests, 1):
        cmd = t['command']
        props = {p['name']: p['value'] for p in t.get('properties', [])}
        env = dict(e.split('=', 1) for e in props.get('ENVIRONMENT', []))
        cwd = props.get('WORKING_DIRECTORY', bld)
        exe = cmd[0].endswith('.exe')
        if exe:
            ran_wine += 1
            args = ['env', 'WINEPATH=C:\\bb',
                    'CT_TEST_FONT=Z:/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf'] + [f'{k}={zpath(v)}' for k, v in env.items()]
            args += ['wine'] + [zpath(a) for a in cmd]
            full = [os.path.join(here, 'wine.sh'), out, '--'] + args
            penv = dict(os.environ, CT_WINE_PODMAN_ARGS=f'-v {repo}:{repo}:ro -v {bld}:{bld} -v {out}/testbin/cmd.exe:/home/wine/prefix/drive_c/windows/system32/cmd.exe:ro -e WINEDLLOVERRIDES=cmd.exe=n -w {cwd}')
            where = 'wine'
        else:
            ran_box += 1
            full = ['distrobox', 'enter', 'ct-build', '--', 'env'] + \
                   [f'{k}={v}' for k, v in env.items()] + cmd
            penv = None
            where = 'box'
        try:
            p = subprocess.run(full, cwd=cwd if not exe else None, env=penv, capture_output=True,
                               text=True, timeout=TIMEOUT)
            rc, text = p.returncode, p.stdout + p.stderr
        except subprocess.TimeoutExpired as e:
            rc, text = -1, f'timeout after {TIMEOUT}s'
        rx = props.get('PASS_REGULAR_EXPRESSION')
        if rx:
            ok = any(re.search(x, text) for x in rx)
        else:
            ok = rc == 0
        skip = props.get('SKIP_RETURN_CODE')
        if skip is not None and rc == int(skip):
            skipped.append(t['name'])
            print(f'{i:3}/{len(tests)} {t["name"]} ({where}) ***Skipped')
            continue
        print(f'{i:3}/{len(tests)} {t["name"]} ({where}) {"Passed" if ok else "***Failed"}')
        if ok:
            passed.append(t['name'])
        else:
            failed.append(t['name'])
            print('\n'.join('    | ' + l for l in text.splitlines()[-25:]))
    n = len(tests)
    print(f'{ran_wine} tests ran under Wine, {ran_box} script tests ran in the build box')
    if skipped:
        print(f'skipped (their own SKIP_RETURN_CODE): {", ".join(skipped)}')
    good = len(passed) + len(skipped)
    if failed:
        print(f'{100 * good // n}% tests passed, {len(failed)} tests failed out of {n}')
        print('The following tests FAILED: ' + ', '.join(failed))
        return 1
    if n != expect:
        print(f'test count {n} differs from the Linux engine-only build ({expect})')
        return 1
    print(f'100% tests passed, 0 tests failed out of {n}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
