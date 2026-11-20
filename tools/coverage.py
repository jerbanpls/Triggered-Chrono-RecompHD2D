#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Function coverage of the replays: which known functions does no replay run?

Known functions are the addresses in game/ct/funcs.toml minus the labels in
notcode.toml (not entries). Each replay in game/ct/test/replay/ is run with
ct_boot; the executed addresses of all runs are unioned and compared.

Two ways to see what ran (`--mode`):
  first-exec  (default) `ct_boot --interp-only --first-exec FILE`: every
              instruction address that ran, so a function counts as executed
              when its entry address ran. Exact, but interpreted (slow).
  profile     `ct_boot --interp-only --profile N`: the entries the profile
              charged instructions to (targets of JSR/JSL/JSR (a,X), interrupt
              entries). Misses a function entered only by JMP or fall-through.
Native runs cannot be used: the native share is counted per instruction,
never per function, so natively run functions leave no address behind.

usage: coverage.py [--boot PATH] [--rom ROM] [--mode first-exec|profile]
                   [--replays DIR] [--game DIR] [--jobs N] [--summary]
       coverage.py --from-output FILE... [--game DIR]   (no ROM: read the
                   outputs of earlier runs, one file per replay, either mode)

Prints executed / never executed / total, then the never-executed functions
grouped by bank (--summary: only the counts and a per-bank tally).
Exit 0; 2 on bad usage; 1 when a replay run fails.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
import tempfile
import tomllib

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SLACK = 150   # frames run past the last frame a script mentions

# Extra ct_boot arguments by replay name prefix (see game/ct/CMakeLists.txt).
EXTRA = (("coop_", ["--coop"]),)


def load_addrs(path, key):
    with open(path, "rb") as f:
        return tomllib.load(f).get(key, [])


def known_functions(game):
    """{address: name} of funcs.toml without notcode.toml's labels."""
    funcs = {}
    for e in load_addrs(os.path.join(game, "funcs.toml"), "func"):
        funcs.setdefault(e["addr"], e.get("name", "$%06X" % e["addr"]))
    p = os.path.join(game, "notcode.toml")
    if os.path.exists(p):
        for e in load_addrs(p, "notcode"):
            funcs.pop(e["addr"], None)
    return funcs


def parse_first_exec(text):
    """First-exec file: 'ADDR clock' per line (hex address)."""
    out = set()
    for line in text.splitlines():
        m = re.match(r"([0-9A-Fa-f]{6})\s", line)
        if m:
            out.add(int(m[1], 16))
    return out


def parse_profile(text):
    """--profile output: 'profile: PCT%  $ADDR mXxXeX name reason' rows."""
    out = set()
    for line in text.splitlines():
        m = re.match(r"profile:\s+[\d.]+%\s+\$([0-9A-Fa-f]{6})\s+m\dx\de\d", line)
        if m and int(m[1], 16):
            out.add(int(m[1], 16))
    return out


def parse_any(text):
    return parse_profile(text) if re.search(r"^profile:", text, re.M) else parse_first_exec(text)


def script_frames(path):
    """Frames a replay needs: the last frame its spans or resets name, plus slack."""
    last = 0
    with open(path) as f:
        for line in f:
            line = line.split("#", 1)[0]
            for m in re.finditer(r"(\d+)-(\d+):", line):
                last = max(last, int(m[2]))
            m = re.match(r"\s*reset\s+(\d+)", line)
            if m:
                last = max(last, int(m[1]))
    return last + SLACK


def run_replay(boot, rom, script, mode, tmp):
    name = os.path.splitext(os.path.basename(script))[0]
    cmd = [boot, "--frames", str(script_frames(script)), "--script", script,
           "--interp-only", "--overlay-fallback"]
    for prefix, extra in EXTRA:
        if name.startswith(prefix):
            cmd += extra
    fe = os.path.join(tmp, name + ".first")
    cmd += ["--first-exec", fe] if mode == "first-exec" else ["--profile", "100000"]
    env = dict(os.environ, SDL_AUDIODRIVER="dummy", CT_ROM=rom)
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode:
        return name, None, f"exit {r.returncode}: {(r.stderr or r.stdout).strip()[-300:]}"
    text = open(fe).read() if mode == "first-exec" else r.stdout
    return name, parse_any(text), ""


def report(funcs, executed, summary, out=sys.stdout):
    hit = {a for a in funcs if a in executed}
    miss = sorted(set(funcs) - hit)
    print(f"executed {len(hit)}  never executed {len(miss)}  total {len(funcs)}"
          f"  ({100.0 * len(hit) / max(1, len(funcs)):.1f}% executed)", file=out)
    banks = {}
    for a in miss:
        banks.setdefault(a >> 16, []).append(a)
    for b in sorted(set(a >> 16 for a in funcs)):
        total = sum(1 for a in funcs if a >> 16 == b)
        print(f"bank ${b:02X}: {total - len(banks.get(b, []))}/{total} executed", file=out)
    if not summary:
        for b in sorted(banks):
            print(f"\nnever executed, bank ${b:02X} ({len(banks[b])}):", file=out)
            for a in banks[b]:
                print(f"  ${a:06X} {funcs[a]}", file=out)
    return len(miss)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--boot")
    ap.add_argument("--rom")
    ap.add_argument("--mode", choices=("first-exec", "profile"), default="first-exec")
    ap.add_argument("--game", default=os.path.join(REPO, "game", "ct"))
    ap.add_argument("--replays")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--summary", action="store_true")
    ap.add_argument("--from-output", nargs="+", metavar="FILE")
    a = ap.parse_args(argv)
    funcs = known_functions(a.game)
    executed = set()
    if a.from_output:
        for p in a.from_output:
            executed |= parse_any(open(p).read())
    else:
        if not (a.boot and a.rom):
            ap.error("--boot and --rom are required (or --from-output)")
        scripts = sorted(glob.glob(os.path.join(a.replays or os.path.join(a.game, "test", "replay"), "*.txt")))
        if not scripts:
            ap.error("no replays found")
        bad = 0
        with tempfile.TemporaryDirectory() as tmp, \
                concurrent.futures.ThreadPoolExecutor(max(1, a.jobs)) as ex:
            for name, got, err in ex.map(lambda s: run_replay(a.boot, a.rom, s, a.mode, tmp), scripts):
                if got is None:
                    print(f"coverage: {name} failed: {err}", file=sys.stderr)
                    bad = 1
                    continue
                print(f"coverage: {name}: {len(got & set(funcs))} known functions", file=sys.stderr)
                executed |= got
        if bad:
            return 1
    report(funcs, executed, a.summary)
    return 0


if __name__ == "__main__":
    sys.exit(main())
