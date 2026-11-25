<p align="center"><img src="packaging/branding/triggered-chrono-stacked.png" alt="Triggered Chrono" width="420"></p>

# Triggered Chrono

**Chrono Trigger, rebuilt as a native PC game from your own copy, with an optional HD-2D look.**
Not an emulator wrapper: the game's 65816 code is statically recompiled to C and compiled for your machine.
Linux first, Steam Deck and ROG Ally tuned, Windows and Android on the way.

[![ci](https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D/actions/workflows/ci.yml/badge.svg)](https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D/actions/workflows/ci.yml)
[![port](https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D/actions/workflows/port.yml/badge.svg)](https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D/actions/workflows/port.yml)
[![release](https://img.shields.io/github/v/release/jerbanpls/Triggered-Chrono-RecompHD2D?include_prereleases&label=latest)](https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D/releases)

> **Not affiliated.** This project is not affiliated with, authorized by or endorsed by the rights holders of
> Chrono Trigger. Game and company names are trademarks of their respective owners and are used only to say what
> the software works with.
>
> **Bring your own game.** It contains no game code, data, art or music. You supply your own legally obtained,
> headerless 4 MB US 1.0 dump; the game module is built on your machine from it, once. No ROM, save, extracted
> asset or generated game code is ever committed or released here, and we will not link to places to get one.

## The goal

A true HD-2D Chrono Trigger: the original game running as native code, drawn like the modern HD-2D remakes,
a miniature stage, warm light, soft depth of field, crisp pixel art, built entirely from what is already in the
player's own ROM. No new art is required for the default look. We want it fast on handhelds, comfortable on a
controller, and open enough that anyone can help finish it.

## Where it stands (alpha, be honest with us)

| | |
|---|---|
| Plays | The original game from the intro through the replays we test: Leene Square, the first battle, the world map, the field menu. Whole-game coverage is **not proven yet** (that is the next big milestone, see below). |
| Looks | The original picture by default. HD-2D (flat or 3D diorama) is one switch away: menu, Look tab, or `--hd2d`. Presets from light to full, tuned for the Steam Deck and ROG Ally. |
| Recompiled | Every function we have found (2115) is native C, and 99.8% to 99.9% of the instructions in each of our replays run as native code. The built-in interpreter covers the rest, so the game is complete either way. "Found so far" is the catch: only recordings of the whole game can show what we have not reached. |
| Comfort | Quick menu on F1 (time, look, overlays, controls and save tiles), rewind, slow motion, frame step, save backups, corner overlays (FPS, clock, battery, play time), A/B and X/Y swap, player lights on pads that have them, touch controls. |
| Not yet | Windows, Android and Flatpak builds are not published. Controller work for the 2026 Steam Controller is planned. Expect rough edges. |

![Progress to a fully native Chrono Trigger: every known function compiled, 99.8% to 99.9% of instructions native on the recorded replays, whole-game coverage not yet measured](docs/progress.svg)

*What the bars mean:* the first is how many of the game's known routines are compiled to native C ("known" means found so far; 260 more labels were
checked and are data, not code). The next four are how much of what actually executes in each replay we recorded runs as that compiled code instead of
the interpreter. The empty bar is the honest part: we cannot claim the whole game until it is recorded.
Source data: [game/ct/progress_runs.toml](game/ct/progress_runs.toml); regenerate with `python3 tools/progress_svg.py`.

Download the latest pre-release from [Releases](https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D/releases)
(AppImage for Linux x86_64, plus the engine tarball and checksums). Then follow the [player's guide](docs/USER-GUIDE.md).
On a Steam Deck or ROG Ally use [docs/HANDHELD.md](docs/HANDHELD.md).

## How it works

1. **Static recompilation.** `recomp/` reads your ROM, decodes every known function with static tracking of the 65816's
   variable register widths, and emits C. `runtime/` supplies the CPU model, bus, cycle model, frame scheduler and the
   SNES hardware (PPU, DMA, audio cores). The build runs on your machine, about 40 seconds, once.
2. **An interpreter as the safety net and the referee.** Code that lives in RAM or patches itself, and anything the
   decoder cannot prove, runs in a small interpreter. The same interpreter is the oracle: tests run native and
   interpreted side by side and require identical state hashes on every frame of every replay.
3. **An HD-2D renderer** (`frontend/hd2d/`). The game keeps a layered scene; we classify its layers and sprites,
   rebuild a stage with depth, and apply tilt-shift blur, bloom, soft shadows and a filmic grade. Optional 3D diorama
   looks add camera tilt and, on the Deck-class presets, a lighter cost.
4. **Engine and game stay separate.** `recomp/`, `runtime/` and `frontend/` do not know the game; everything about
   Chrono Trigger lives in `game/ct/` as data and hooks. Another SNES game could, in principle, reuse the engine.

## Our research (read it, challenge it, extend it)

We write down what we learn, with sources and with what we have not verified. Start here:

| Topic | Document |
|---|---|
| How similar projects turn old games into HD, 3D and co-op; lessons from other recompilations | [docs/PRIOR-ART.md](docs/PRIOR-ART.md), [docs/COOP-PRIOR-ART.md](docs/COOP-PRIOR-ART.md) |
| What "right" looks like for the HD-2D and diorama looks, and the QA rules | [docs/VISUAL-DESIGN.md](docs/VISUAL-DESIGN.md), [docs/QA-2D.md](docs/QA-2D.md), [docs/QA-3D.md](docs/QA-3D.md), [docs/QA-WORLD.md](docs/QA-WORLD.md), [docs/QA-AUDIT.md](docs/QA-AUDIT.md) |
| The renderer: planes, depth, blur, bloom, presets, debug views | [docs/HD2D.md](docs/HD2D.md) |
| What the game keeps in memory that a renderer can use to rebuild a scene (OAM, layers, heights) | [docs/SCENE.md](docs/SCENE.md) |
| R1: geometry hints and the 3D-scene correctness items (where the diorama is wrong, and how to fix it) | [docs/research/GEOMETRY.md](docs/research/GEOMETRY.md) |
| R2: how the commercial HD-2D games build the look, mapped to our renderer | [docs/research/LOOK.md](docs/research/LOOK.md) |
| R3: asset packs: upscalers, material maps, loaders, what other emulators really support | [docs/research/ASSETS.md](docs/research/ASSETS.md) |
| R4: native controller support for Nintendo, Xbox, PlayStation, the 2026 Steam Controller and the Deck, checked against SDL source | [docs/research/INPUT.md](docs/research/INPUT.md) |
| Engine and game module split; code in RAM (overlays); patches | [docs/MODULES.md](docs/MODULES.md) |
| Why the code is GPL-only | [docs/GPL-REWRITE.md](docs/GPL-REWRITE.md) |
| Open problems we know about | [docs/KNOWN-ISSUES.md](docs/KNOWN-ISSUES.md) |
| Where it is going | [ROADMAP.md](ROADMAP.md) |

## What we are working on next

- **Finish the recompile and make the interpreter unnecessary in play.** Merge the next compiler round (above 99% native on
  our replays), record the whole game in chapters, list every function no recording reaches, add a trap build that reports
  each entry into the interpreter, and close that list. The interpreter stays as the test referee and last-resort fallback.
- **HD-2D correctness.** The scene classification and 3D geometry items in R1: where the stage is built wrong, UI that must
  stay flat, a level solver for heights.
- **Controllers, natively.** Bundle a newer SDL so the 2026 Steam Controller works without Steam; player LEDs, GameCube and
  third-party pad glyphs, gyro and touchpad pointers, per-player button pictures.
- **Quick menu tiles** for the new rewind, speed and overlay features, then achievements (RetroAchievements), widescreen and
  more from the [roadmap](ROADMAP.md).

## Help us build it

You do not need to write C to help. Pick a lane:

- **Play and report.** The most useful thing right now. Tell us your GPU, distro, the look you used, and your controller.
  Open an [issue](https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D/issues). Never attach a ROM, a save or a
  screenshot you did not take yourself.
- **Test hardware.** Steam Deck, ROG Ally, Legion Go, DualSense, Switch Pro, Xbox and the 2026 Steam Controller. The
  checklists are in [docs/HANDHELD.md](docs/HANDHELD.md); we mark what we could not verify as unverified, so anything you
  confirm or break is valuable.
- **Record the game.** Whole-game coverage needs recordings of full playthroughs, kept on your own machine (they stay
  out of the repo). `ct_sdl --record` already saves your input as a script; chapter tooling is landing next.
- **Read the research and push back.** Find a hole in R1 to R4, a better way to do depth of field, a source we missed.
  Open an issue or send a patch to the doc.
- **Make art for packs, with your own work.** Replacement art, fonts and music load from packs
  ([docs/ASSET-PACKS.md](docs/ASSET-PACKS.md)). Only art you made or have the right to share.
- **Write code.** Good places to start: the HD-2D renderer and its debug views (`frontend/hd2d/`), the recompiler's decoder
  and emitter (`recomp/`), tests (`test/`), tools (`tools/`), docs. Read [docs/MODULES.md](docs/MODULES.md) first.

**Ground rules for every contribution**
1. No game data in a pull request: no ROM bytes, extracted art or music, saves, states, recordings, dumps or generated C.
   `tools/check_dist.py` and the CI check enforce this.
2. Licence: GPL-3.0-or-later. New files carry an SPDX header; third-party code only if its licence is compatible, with its
   notice kept and listed in `THIRD_PARTY.md`.
3. The engine stays game-agnostic: `python3 tools/check_agnostic.py .` must be clean.
4. Tests stay honest: never weaken, skip or loosen one to get a green run. A failing test is information.
5. Original names and our own words. No promotion of this project as a way to play without owning the game.

## Build and run from source

Requirements: CMake 3.20+, a C11 compiler, Python 3.11+, SDL2 for the window.

```
export CT_ROM=/path/to/chrono_trigger.sfc    # your own headerless 4 MB US 1.0 dump; git-ignored
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
build/ct_sdl                                 # play (add --hd2d for the HD-2D look)
```

Engine only, no ROM (what CI runs): `cmake -S . -B build && cmake --build build -j && ctest --test-dir build -LE rom`.
More on the programs, tools and layout is in the developer reference at the end of this file.

## Where it started, and what has changed

Triggered Chrono began in late September 2026 from the code of [ct-recomp](https://github.com/ThisIsAkill/ct-recomp) by
Akhil Moola, as it stood on 2026-09-29 (commit `a6b4925`). Here is what that starting point contained and where this project is now,
measured on the same repository.

| | At the start (ct-recomp, 2026-09-29) | Now (Triggered Chrono) |
|---|---|---|
| Known functions compiled to C | 1,414 of 2,036 (69.4%) | every function found so far: 2,115 (260 labels checked and reclassified as data) |
| Instructions running as native code, 1500-frame boot | 97.6% | 99.8% |
| Replays measured | boot only | boot, Leene Square, the first battle, the world map, the field menu: 99.8% to 99.9% native |
| Automated tests | 67 | about 140 |
| Files in the repository | 135 | about 570 |
| Frontend | SDL2 window and input | quick menu, rewind, slow motion, frame step, overlays, save backups, controller layer (glyphs, rumble, Steam Deck and ROG Ally presets), touch, asset and audio packs, HD text, optional HD-2D renderer |
| Releases | none | three Linux pre-releases (AppImage), a Windows port in progress, Android scaffolding in the tree |

What this does not say: both projects keep moving, and the left column is only what we received. The whole-game native claim is
still unproven: our recordings reach about two thirds of the known functions, and the SNES picture and sound cores are still
third-party code that we plan to replace one piece at a time ([docs/GPL-REWRITE.md](docs/GPL-REWRITE.md)). Of the 405 commits in this
repository, 165 came with the import and 240 were made here.

## Credits

This project started from [ct-recomp](https://github.com/ThisIsAkill/ct-recomp) by Akhil Moola (MIT, copyright 2026 Akhil Moola);
his copyright notice and licence stay with every file that came from it ([LICENSES/MIT.txt](LICENSES/MIT.txt), [CREDITS.md](CREDITS.md)). Includes the zelda3 SNES core (MIT)
and the ares SPC700 core (ISC). Symbol names are partly derived from dscotton/ct_disassembly (public domain) and
ChronoRET (MIT); [Mesen 2](https://github.com/SourMesen/Mesen2) is used as a reference tool. Thanks to the TASVideos
community for the input movies we compare against, and to everyone who documented the SNES and this game.
The file-by-file licence map is in [REUSE.toml](REUSE.toml), the texts in [LICENSES/](LICENSES/), the details in
[THIRD_PARTY.md](THIRD_PARTY.md).

## License

GNU General Public License, version 3 or (at your option) any later version: see [LICENSE](LICENSE). It includes code under
other, GPL-compatible licences which keep their original notices (see Credits).

---

## Developer reference

The notes below are the engine's own documentation: layout, build options, replays, reference comparison. The recompiler
and runtime are game-agnostic; everything specific to Chrono Trigger lives in `game/ct/`.

Planned features after boot (MSU-1, achievements, save states, widescreen and
more) are listed in [ROADMAP.md](ROADMAP.md).

## Progress numbers

<!-- progress:start -->

**100.0% of the functions found so far are compiled**: 2115 of 2115 known functions are compiled to C. "Known" means found by the recompiler and the replays; whole-game coverage is a separate, unmeasured claim (see the progress picture above).

- Native at runtime: 99.8% of the instructions in a 1500-frame boot run as compiled C (the rest run in the interpreter).

Updated by `tools/progress.py` with every push; details in [game/ct/PROGRESS.md](game/ct/PROGRESS.md).

<!-- progress:end -->

## Layout

- `recomp/` — the translator (Python): decoder with static M/X tracking, C emitter.
- `runtime/` — CPU, bus, cycle model, interpreter, frame scheduler; the vendored
  PPU/DMA/APU core lives in `third_party/snes/`.
- `frontend/` — SDL2 frontend; `tools/ct_boot.c` — headless boot probe.
- `test/` — engine tests; they run on a synthetic ROM generated at build time.
- `game/ct/` — everything specific to Chrono Trigger: `game.toml` (ROM identity),
  `funcs.toml` / `unresolved.toml` (symbols), extern hooks, game tools, the
  ROM-dependent tests, and `PROGRESS.md`.

## Build

Requirements: CMake 3.20+, a C11 compiler, Python 3.11+, SDL2 (optional, for
`ct_sdl`).

Engine only (no ROM needed; what CI runs):

```
cmake -S . -B build && cmake --build build -j && ctest --test-dir build -LE rom
```

With the game. Supply your own headerless US 1.0 ROM; it is checked against
`game/ct/game.toml` at configure time:

```
export CT_ROM=/path/to/chrono_trigger.sfc   # headerless, 4 MB
cmake -S . -B build && cmake --build build -j && ctest --test-dir build
```

This builds `game/ct` (generated C goes to `build/game/ct/out/`) and two
programs at the top of `build/`. For investigations, a Debug tree builds in
about 30 seconds (runtime at `-Og`, generated code at `-O1`, see `CT_GEN_OPT`):
`cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=Debug`. The build uses ccache when
it is installed.

```
build/ct_sdl                           # play: window, audio, keyboard/controller
mkdir -p shots && build/ct_boot --frames 1500 --dump shots   # headless: PNGs of what changed
```

`ct_sdl` keys: arrows, Z=B, X=A, A=Y, S=X, Q=L, W=R, Enter=Start, Right Shift=Select;
a game controller works too. Ctrl+Q or closing the window quits.
On a touchscreen an on-screen controller appears when you touch the window
(`--touch` shows it from the start and lets the mouse press it; see
[docs/ANDROID-BUILD.md](docs/ANDROID-BUILD.md#the-touch-controller)).
`ct_sdl --record FILE` saves your pad input as a script; `ct_boot --script FILE
--frames N` replays it exactly (N is on its last line), and `ct_sdl --script
FILE` plays it back in the window. A `reset F` line in a script presses the reset button as
frame F begins. `ct_boot` also takes `--input F1-F2:BUTTONS`
to script the pad, `--wav FILE` for audio, `--hash-log FILE` for per-frame
state hashes (`tools/lockstep.py` compares native and interpreted runs with
them), and `--needed-hw FILE` to report where emulation stops.

Engine-only (`build/ct_sdl_engine`, no game code inside, see
[docs/MODULES.md](docs/MODULES.md)): started without `--module`, it finds your
ROM (`--rom FILE`, then `$CT_ROM`, then the one it used last, else it asks you
to drop the file on its window), checks it against `game/ct/game.toml`, builds
the game module from it into its preferences folder
(`~/.local/share/ct-recomp/ct/`) with a progress screen, about 40 seconds once,
and starts the game. Later runs reuse the module until the ROM or the module
ABI changes. If the build fails, a key press plays in the interpreter instead.

```
build/ct_sdl_engine --hd2d --rom /path/to/chrono_trigger.sfc
build/ct_sdl_engine --hd2d             # later: the ROM and module are remembered
```

`--data-dir DIR` points it at an installed copy of `game/` and `tools/` (default:
this source tree; packagers set `-DCT_ENGINE_DATA_DIR=...`). Before shipping a
package, `tools/check_dist.py PACKAGE --rom ROM` checks it for anything built
from the ROM, inside `.zip`, `.tar` and `.AppImage` files too.

Reference comparison: `tools/ref_compare.py --probe build/ct_boot --mesen PATH
--rom $CT_ROM --frames N [--script FILE]` runs the same input in
[Mesen 2](https://github.com/SourMesen/Mesen2) (used as a tool, headless under
`xvfb-run`, with a private settings folder) and reports the first frame where
WRAM or the picture differs, with the differing WRAM ranges.
`tools/trace_diff.py --probe build/ct_boot --mesen PATH --rom $CT_ROM --from F
--to G [--script FILE]` runs both untraced up to frame F and compares every
instruction of frames F-G (address, clock, X, Y), reporting the first
difference; about a minute for a window 12,000 frames in.
`tools/tas_convert.py MOVIE OUT` turns a TASVideos BizHawk `.bk2` or lsnes
`.lsmv` movie (from power-on) into an input script for both, resets included. Replays of a new
game into Leene Square and into the first battle are in `game/ct/test/replay/`.

The ROM is never committed, and CI never sees it.

Symbol names partly derived from dscotton/ct_disassembly (public domain) and ChronoRET.

The ROM is never committed, and CI never sees it.
