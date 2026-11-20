#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/coverage.py on synthetic data, no ROM: both output formats, the union
over replays, notcode labels left out, per-bank grouping, script frame counts."""
import os
import subprocess
import sys
import tempfile

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COV = os.path.join(REPO, "tools", "coverage.py")

FUNCS = """
[[func]]
name = "A_One"
addr = 0xE10000
[[func]]
name = "A_One"
addr = 0xE10000
states = ["m0x0"]
[[func]]
name = "B_Two"
addr = 0xE10010
[[func]]
name = "C_Three"
addr = 0xE20000
[[func]]
name = "D_Four"
addr = 0xE20020
[[func]]
name = "Label"
addr = 0xE20040
"""
NOTCODE = """
[[notcode]]
name = "Label"
addr = 0xE20040
kind = "interior"
"""
FIRST = "E10000 12\nE10005 40\nE20020 99\nFFFFFF 1\n"
PROFILE = ("profile: 100 instructions, 0.00% native (0 native, 100 interpreted)\n"
           "profile:  50.00%  $E10010 m1x1e0 B_Two        overlay\n"
           "profile:  10.00%  $000000 m0x0e0              outside any call (reset code, main loop)\n")


def run(*args):
    return subprocess.run([sys.executable, COV, *args], capture_output=True, text=True)


def main():
    fails = []
    with tempfile.TemporaryDirectory() as d:
        os.mkdir(os.path.join(d, "g"))
        for n, t in (("funcs.toml", FUNCS), ("notcode.toml", NOTCODE)):
            open(os.path.join(d, "g", n), "w").write(t)
        f1, f2 = os.path.join(d, "a.first"), os.path.join(d, "b.prof")
        open(f1, "w").write(FIRST)
        open(f2, "w").write(PROFILE)
        game = os.path.join(d, "g")

        r = run("--game", game, "--from-output", f1)
        if r.returncode or "executed 2  never executed 2  total 4" not in r.stdout:
            fails.append("first-exec counts: " + r.stdout + r.stderr)
        if "$E10010 B_Two" not in r.stdout or "$E20000 C_Three" not in r.stdout:
            fails.append("never-executed list")
        if "Label" in r.stdout:
            fails.append("notcode label counted")
        if "bank $E1: 1/2 executed" not in r.stdout or "bank $E2: 1/2 executed" not in r.stdout:
            fails.append("bank tally")
        r = run("--game", game, "--from-output", f2)
        if "executed 1  never executed 3  total 4" not in r.stdout:
            fails.append("profile counts: " + r.stdout)
        r = run("--game", game, "--from-output", f1, f2)   # union
        if "executed 3  never executed 1  total 4" not in r.stdout or "$E20000 C_Three" not in r.stdout:
            fails.append("union: " + r.stdout)
        r = run("--game", game, "--from-output", f1, "--summary")
        if "never executed, bank" in r.stdout:
            fails.append("--summary lists functions")
        if run("--game", game).returncode != 2:
            fails.append("missing --boot/--rom accepted")

        sys.path.insert(0, os.path.join(REPO, "tools"))
        import coverage
        s = os.path.join(d, "s.txt")
        open(s, "w").write("# 5-9999: comment\n10-20:a\np2 30-400:b\nreset 500\n")
        if coverage.script_frames(s) != 500 + coverage.SLACK:
            fails.append("script_frames")
    for f in fails:
        print("FAIL:", f)
    print("coverage: %s" % ("FAIL" if fails else "ok"))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
