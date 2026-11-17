<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Recording replay chapters

Long input recordings can be split into short chapters linked by save states. Each
chapter has its own input script and frame duration; replaying the chain loads the
previous chapter's end state before applying the next script. This makes a whole-game
run practical to record and replay without putting one long recording in the repository.

Use your own legally obtained ROM. Keep the ROM, recordings, manifests that refer to
your run, and save states outside the repository (or in a locally ignored directory).
**Do not commit or upload any of them.** Save states contain game data. The examples use
`recordings/` as a directory outside the source tree; adjust the paths for your machine.

These instructions use the single-player frontend and probe. Do not add `--coop` to a
recording chain.

## Build and record the first chapter

Configure and build with `CT_ROM` pointing to your ROM, then create a working directory:

```sh
CT_ROM=/path/to/your/rom.sfc cmake -S . -B build -G Ninja
cmake --build build --target ct_sdl ct_boot -j6
mkdir -p recordings/states
```

Play the first chapter in the frontend. `--frames 3600` stops at that frame and closes
the recording; choose the chapter length you want. `--no-save` prevents changes to your
normal in-game save files, and `--no-resume` makes the first chapter start from reset.

```sh
build/ct_sdl --record recordings/01.txt --frames 3600 --no-save --no-resume
```

Create its end state by replaying the recording headlessly:

```sh
build/ct_boot --script recordings/01.txt --frames 3600 \
    --save-state 3600:recordings/states/01.ctstate
```

`ct_boot` writes the state at the first frame edge from the requested frame onward that
can take one. Its output names the actual frame and file.

## Record another chapter

Start the frontend from the previous end state and record only the new play. For a
second chapter 3600 frames long, the ending frame is 7200:

```sh
build/ct_sdl --load-state recordings/states/01.ctstate \
    --record recordings/02.txt --frames 7200 --no-save
build/ct_boot --load-state recordings/states/01.ctstate \
    --script recordings/02.txt --frames 7200 \
    --save-state 7200:recordings/states/02.ctstate
```

After a loaded state, `--frames` and the script's input frame numbers remain absolute;
they do not restart at 1. If the first state is at frame 3600, a chapter lasting 600
frames ends at frame 4200. Repeat these steps with the corresponding cumulative ending
frame for each chapter. Use the same ROM and a compatible engine throughout: a state
from another ROM or an incompatible engine version is refused.

## Write a manifest and run the chain

A manifest is TOML with one `[[chapter]]` per chapter. Paths are relative to the
manifest. `frames` is each chapter's duration, not its absolute ending frame.
`start_state` is optional for the first chapter (omit it to start from reset). Later
chapters always receive the previous chapter's actual end state; repeating
`start_state` as below is optional and checks that the manifest names the right file.

```toml
[[chapter]]
name = "opening"
script = "01.txt"
frames = 3600
end_state = "states/01.ctstate"

[[chapter]]
name = "next-area"
script = "02.txt"
frames = 3600
start_state = "states/01.ctstate"
end_state = "states/02.ctstate"
```

Run every chapter in order and print its native-execution profile (the number is how
many interpreted hotspots each report lists):

```sh
python3 tools/chapters.py build/ct_boot recordings/chapters.toml --profile 10
```

The runner stops at the first bad state, mismatched start state, failed probe, or
missing end state. It prints each chapter's actual ending frame and state path, followed
by a completion line after the whole chain. Omit `--profile` for just the chapter and
state summaries.
