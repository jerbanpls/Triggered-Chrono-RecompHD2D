#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run input scripts as a save-state-linked chain of chapters.

usage: chapters.py PROBE MANIFEST [--profile N]

MANIFEST is TOML with one [[chapter]] per chapter. Each entry has a name,
script, a positive frame duration and an end_state. The first chapter may
have a start_state; without one it starts from reset. Later chapters start
from the previous chapter's end state. A start_state may be repeated on a
later chapter as a check, but must name that previous end state. Relative
paths are resolved from the manifest's directory.

--profile N passes ct_boot's --profile N to every chapter and prints each
chapter's native-share report. Exit 0 after the whole chain, 1 on a bad
manifest, state or failed chapter, 2 on bad command-line usage.
"""
import argparse
import os
import struct
import subprocess
import sys
import tomllib

MAGIC = b"CTSTATE\x1a"
HEADER_LEN = 44


class Unusable(Exception):
    pass


def state_frame(path):
    try:
        with open(path, "rb") as f:
            head = f.read(HEADER_LEN)
    except OSError as e:
        raise Unusable(f"{path}: {e.strerror}") from e
    if len(head) < HEADER_LEN or head[:8] != MAGIC:
        raise Unusable(f"{path}: not a save state")
    return struct.unpack_from("<Q", head, 36)[0]


def manifest_path(base, value):
    return os.path.abspath(os.path.join(base, value))


def load_manifest(path):
    try:
        with open(path, "rb") as f:
            entries = tomllib.load(f).get("chapter", [])
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise Unusable(f"{path}: {e}") from e
    if not isinstance(entries, list) or not entries:
        raise Unusable(f"{path}: no [[chapter]] entries")
    base = os.path.dirname(os.path.abspath(path))
    chapters, names = [], set()
    previous_end = None
    fields = {"name", "script", "frames", "start_state", "end_state"}
    for number, entry in enumerate(entries, 1):
        where = f"{path}: chapter {number}"
        if not isinstance(entry, dict) or not fields.issuperset(entry):
            raise Unusable(f"{where}: unknown fields")
        missing = {"name", "script", "frames", "end_state"} - entry.keys()
        if missing:
            raise Unusable(f"{where}: missing {', '.join(sorted(missing))}")
        name, frames = entry["name"], entry["frames"]
        if not isinstance(name, str) or not name or name in names:
            raise Unusable(f"{where}: name must be nonempty and unique")
        if type(frames) is not int or frames <= 0:
            raise Unusable(f"{where}: frames must be a positive integer")
        if any(not isinstance(entry.get(key), str) or not entry[key]
               for key in ("script", "end_state")):
            raise Unusable(f"{where}: script and end_state must be nonempty paths")
        if "start_state" in entry and (not isinstance(entry["start_state"], str)
                                       or not entry["start_state"]):
            raise Unusable(f"{where}: start_state must be a nonempty path")
        start = manifest_path(base, entry["start_state"]) if "start_state" in entry else None
        end = manifest_path(base, entry["end_state"])
        if previous_end is not None and start is not None and start != previous_end:
            raise Unusable(f"{where}: start_state is not the previous chapter's end_state")
        chapters.append({"name": name, "script": manifest_path(base, entry["script"]),
                         "frames": frames, "start": start, "end": end})
        names.add(name)
        previous_end = end
    return chapters


def run_chapter(probe, chapter, previous_end, profile):
    start = previous_end if previous_end is not None else chapter["start"]
    first = state_frame(start) if start is not None else 0
    last = first + chapter["frames"]
    cmd = [probe, "--frames", str(last), "--script", chapter["script"],
           "--save-state", f"{last}:{chapter['end']}"]
    if start is not None:
        cmd += ["--load-state", start]
    if profile is not None:
        cmd += ["--profile", str(profile)]
    parent = os.path.dirname(chapter["end"])
    if parent:
        os.makedirs(parent, exist_ok=True)
    env = dict(os.environ, SDL_AUDIODRIVER="dummy")
    run = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    if run.returncode:
        print(f"chapters: {chapter['name']}: probe exit {run.returncode}\n{run.stdout}",
              file=sys.stderr)
        raise Unusable(f"chapter {chapter['name']} failed")
    if profile is not None:
        report = False
        for line in run.stdout.splitlines():
            report = report or line.startswith("profile:")
            if report:
                print(f"chapters: {chapter['name']}: {line}")
    actual_last = state_frame(chapter["end"])
    print(f"chapters: {chapter['name']}: frames {first + 1}-{actual_last}, "
          f"state {chapter['end']}")
    return chapter["end"]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("probe")
    ap.add_argument("manifest")
    ap.add_argument("--profile", type=int, metavar="N")
    args = ap.parse_args(argv)
    if args.profile is not None and args.profile < 0:
        ap.error("--profile N must be nonnegative")
    try:
        chapters = load_manifest(args.manifest)
        previous_end = None
        for chapter in chapters:
            previous_end = run_chapter(args.probe, chapter, previous_end, args.profile)
    except (Unusable, OSError) as e:
        print(f"chapters: {e}", file=sys.stderr)
        return 1
    print(f"chapters: {len(chapters)} chapters complete")
    return 0


if __name__ == "__main__":
    sys.exit(main())
