#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/chapters.py with a fake probe and synthetic states: two chapters
chain through the first end state, absolute probe frames include the loaded
frame, native shares are reported, and a bad link stops before any run."""
import json
import os
import stat
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "chapters.py")
MAGIC = b"CTSTATE\x1a"

FAKE = r'''#!/usr/bin/env python3
import json
import os
import struct
import sys

a = sys.argv[1:]
frames = int(a[a.index("--frames") + 1])
load = a[a.index("--load-state") + 1] if "--load-state" in a else None
script = a[a.index("--script") + 1]
target, out = a[a.index("--save-state") + 1].rsplit(":", 1)
assert int(target) == frames
if load:
    before = open(load, "rb").read()
    assert before[:8] == b"CTSTATE\x1a"
    start = struct.unpack_from("<Q", before, 36)[0]
    payload = before[44:]
else:
    start, payload = 0, b"reset"
with open(os.environ["FAKE_CHAPTER_LOG"], "a") as log:
    log.write(json.dumps({"frames": frames, "load": load, "script": script,
                          "save": out, "start_payload": payload.decode()}) + "\n")
head = bytearray(44)
head[:8] = b"CTSTATE\x1a"
struct.pack_into("<Q", head, 36, frames)
open(out, "wb").write(head + payload + b">" + open(script, "rb").read())
print(f"ct_boot: state loaded at {start}")
print("profile: 200 instructions, 75.00% native (150 native, 50 interpreted)")
print("  50 interpreted instructions in synthetic_function")
'''

fails = 0


def check(cond, msg, run=None):
    global fails
    if not cond:
        fails += 1
        print("FAIL", msg)
        if run is not None:
            print(run.stdout, run.stderr)


def state(path, frame, payload):
    head = bytearray(44)
    head[:8] = MAGIC
    struct.pack_into("<Q", head, 36, frame)
    with open(path, "wb") as f:
        f.write(head + payload)


with tempfile.TemporaryDirectory() as tmp:
    probe = os.path.join(tmp, "probe")
    with open(probe, "w") as f:
        f.write(FAKE)
    os.chmod(probe, os.stat(probe).st_mode | stat.S_IEXEC)
    log = os.path.join(tmp, "calls.jsonl")
    env = dict(os.environ, FAKE_CHAPTER_LOG=log)
    start = os.path.join(tmp, "start.ctstate")
    first_end = os.path.join(tmp, "states", "first.ctstate")
    second_end = os.path.join(tmp, "states", "second.ctstate")
    state(start, 40, b"initial")
    for name in ("one", "two"):
        with open(os.path.join(tmp, name + ".txt"), "w") as f:
            f.write(name)
    manifest = os.path.join(tmp, "chapters.toml")
    with open(manifest, "w") as f:
        f.write('''[[chapter]]
name = "first"
script = "one.txt"
frames = 12
start_state = "start.ctstate"
end_state = "states/first.ctstate"

[[chapter]]
name = "second"
script = "two.txt"
frames = 8
start_state = "states/first.ctstate"
end_state = "states/second.ctstate"
''')
    run = subprocess.run([sys.executable, TOOL, probe, manifest, "--profile", "4"],
                         capture_output=True, text=True, env=env)
    check(run.returncode == 0, "two-chapter chain", run)
    with open(log) as f:
        calls = [json.loads(line) for line in f]
    check(len(calls) == 2, "two probe calls")
    if len(calls) == 2:
        check(calls[0]["frames"] == 52 and calls[1]["frames"] == 60,
              f"durations added to loaded frames: {calls}")
        check(calls[0]["load"] == start, "initial state loaded")
        check(calls[1]["load"] == first_end and calls[1]["start_payload"] == "initial>one",
              "second chapter starts from first end state")
        check(calls[0]["save"] == first_end and calls[1]["save"] == second_end,
              "chapter end-state paths")
    check(run.stdout.count("75.00% native") == 2 and "first: profile:" in run.stdout
          and "second: profile:" in run.stdout, "per-chapter native shares", run)
    check(open(second_end, "rb").read()[44:] == b"initial>one>two",
          "synthetic state passed through both chapters")
    state_frame = struct.unpack_from("<Q", open(second_end, "rb").read(44), 36)[0]
    check(state_frame == 60, "second end-state frame")

    bad = os.path.join(tmp, "bad.toml")
    with open(bad, "w") as f:
        f.write(open(manifest).read().replace('start_state = "states/first.ctstate"',
                                              'start_state = "start.ctstate"'))
    os.remove(log)
    run = subprocess.run([sys.executable, TOOL, probe, bad], capture_output=True, text=True, env=env)
    check(run.returncode == 1 and "not the previous chapter's end_state" in run.stderr
          and not os.path.exists(log), "mismatched link refused before running", run)

    reset_manifest = os.path.join(tmp, "reset.toml")
    with open(reset_manifest, "w") as f:
        f.write('''[[chapter]]
name = "reset"
script = "one.txt"
frames = 5
end_state = "states/reset.ctstate"
''')
    run = subprocess.run([sys.executable, TOOL, probe, reset_manifest],
                         capture_output=True, text=True, env=env)
    with open(log) as f:
        reset_call = json.loads(f.read())
    check(run.returncode == 0 and reset_call["load"] is None and reset_call["frames"] == 5
          and reset_call["start_payload"] == "reset", "first chapter can start from reset", run)

print(f"chapters: {'ok' if not fails else str(fails) + ' failed'}")
sys.exit(1 if fails else 0)
