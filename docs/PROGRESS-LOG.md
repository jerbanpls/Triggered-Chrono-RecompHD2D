<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Progress log

## 2026-11-13, afternoon: first playable build (Steam Deck / ROG Ally, Bazzite)

### Play it now on this Ally
- **Installed:** `~/Applications/CT_Recomp-x86_64.AppImage`, plus a "CT Recomp" entry in
  the desktop menu. Your game module is already built in `~/.local/share/ct-recomp/ct/`,
  so the game starts at once.
- **Desktop Mode:** run it from the menu. The plain renderer is the default there; add
  `--hd2d` for the HD-2D look, or press F1 or R3 in game.
- **Game Mode:** add the AppImage to Steam as a non-Steam game. For the steps, run
  `packaging/steam/add-to-steam.sh`, which only prints them; see also
  [HANDHELD.md](HANDHELD.md). Under gamescope it goes fullscreen with HD-2D, and on a
  Steam Deck with the lighter `deck` preset.
- **Controls:** the built-in pad plays the game. Hold View+Menu for 2 s to quit, R3
  cycles the look, L3 cycles the aspect.
- **Saves:** in-game saves now persist, in `~/.local/share/ct-recomp/ct/ct.srm`.

### GitHub repo: created, waiting on one step from you
- **Created:** https://github.com/jerbanpls/Triggered-Chrono-RecompHD2D (private when this
  entry was written; now public). It was still empty then.
- **Why:** the push of `main` was refused. Your `gh` token has the `repo` scope but not
  `workflow`, and GitHub won't accept commits containing `.github/workflows/` without it.
- **To finish:**
  ```
  gh auth refresh -h github.com -s workflow     # opens a browser sign-in
  git push -u origin main
  ```
- **Already done:** `main` holds the verified `hd2d` tip, and `origin` points at the new
  repo. The `krtp` and `upstream` remotes are removed, so only `main` gets pushed.
- **History scan:** 0 of 215 commits contain ROM, module, build-output or screenshot
  files, and no ROM byte windows appear in a `git archive` export.
- **To go public later:** Settings → Danger Zone → Change visibility.

### What landed today (all on `hd2d`, verified after each merge)
- **Decimal-mode CPU arithmetic** (65816 BCD ADC/SBC, per bsnes, module ABI 2).
  - Opening the world-map menu no longer stops the game.
  - The `world_map_save` replay saves in-game, resets and loads the save back.
  - 12 crash-hunt runs of 30k–60k frames each, native and interpreted, found no fatal
    errors. See [KNOWN-ISSUES.md](KNOWN-ISSUES.md).
- **Handheld / Game Mode:**
  - fullscreen, no cursor, the deck preset on a Deck
  - controller-only first run, with a ROM search in `~/Documents`, `~/Downloads` and
    next to the AppImage
  - the quit combo
  - timer pacing at 60.10 Hz by default; vsync with `--pacing vsync`
- **HD-2D performance:** frame time roughly halved, with the UI pixel-identical. On the
  Ally's 780M, GPU-only time is about 2.2 → 1.1 ms (median).
- **Also:** touch controls; our own arm64 coroutine switch, verified under qemu; save
  files; a real Flatpak build; AppImage verification; CI workflow; player's guide;
  package checks.

### Verified on this Ally, real window and audio
- The installed AppImage with the prebuilt module ran 600 frames at 60.10 Hz with 0
  audio underruns.
- `--hd2d` gets a hardware GL 4.6 context on radeonsi. That short run had one 153 ms
  stall and 6 underruns, while the full test suite was loading the machine.
- **Follow-up:** the stall doesn't reproduce on an idle machine, even with Mesa's shader
  cache disabled. In 12 runs of 600 frames, 6 before the change and 6 after, the worst
  frame was 16.78 ms, with 0 underruns. So the load caused it.
  - Even so, `hd2d_warmup` now draws one throwaway frame per preset before audio starts
    (about 5 ms here). That way the driver's first-draw work can't land on frame 1 on a
    slower APU like the Deck's.
  - The frames rendered are byte-identical with and without it.

### Not tested on real hardware yet
- Steam's real Game Mode and Steam Input's physical buttons, including the quit combo.
- Any Steam Deck: GPU detection, the deck preset's speed, module build time.
- VRR at 120 Hz on the Ally.
- Long human play sessions. Our automated runs reach the first battle and the world map
  and save there. Later areas may hit engine gaps not seen yet: report the frame and PC
  from the error message.

### Still open
- **Android:** the APK work is paused partway. The Gradle wrapper and SDL fetch work;
  libmain.so needs `CT_GAME_ID` and `saves.c` in the Android CMake branch. See
  [ANDROID-BUILD.md](ANDROID-BUILD.md).
- **The krtp0028 fork:** never merged. It's no longer needed, since arm64 has our own
  context switch.


## 2026-11-13: overnight session

### Read this first
- **Branch:** all work is on `hd2d`, built on `gpl-base`. Nothing has been pushed and
  `main` is untouched.
- **Previews:** the before/after screenshots are in `shots/hd2d-preview/`, which is
  git-ignored and local only, since the screenshots come from your ROM:
  - `compare_*.png`: split view, with the original pixels on the left and HD-2D on the
    right
  - `hd2d_*.png`: the full HD-2D look
- **Try it:**
  ```
  CT_ROM="$PWD/rom/Chrono Trigger (USA).sfc" cmake -S . -B build-hd && cmake --build build-hd -j
  build-hd/ct_sdl --hd2d          # F1 / R3: preset, F2: debug views, F3 / L3: aspect, F11: fullscreen
  ```
  On Bazzite it builds against the system `libSDL2` with official SDL 2.32.10 headers,
  fetched automatically and SHA-256 pinned.

### Done
1. **Licensing** (`gpl-base`, `ff4bb15`):
   - ct-recomp imported with its full history, and the project relicensed as
     GPL-3.0-or-later.
   - The MIT and ISC parts keep their notices.
   - A `REUSE.toml` file map passes `reuse lint`.
   - The full upstream test suite passed on that tree: 63/63, with `sync_determinism`
     skipped by design.
2. **SDL build on Bazzite** (`842ad07`).
3. **HD-2D renderer** (`59b3083`, `ea03109`), documented in `docs/HD2D.md`:
   - The PPU now outputs a per-pixel layer/priority G-buffer. Plain output is untouched:
     all 9,200 frames of the Leene Square replay are hash-identical to before.
   - A GL 3.3 / GLES 3.0 pipeline:
     - layer classes, with the UI and HUD kept sharp
     - relief lighting, actor shadows, contact occlusion and rim light
     - tilt-shift depth of field that keeps actors sharp
     - bloom, a filmic grade, and side fill around the picture
   - Point lights (`62739cc`): taken from the frame's own light sources and gated by
     local contrast and scene darkness, so interiors glow and daylight barely changes.
     Blacks stay black.
   - Presets, debug views, a split-compare view, controller shortcuts (R3/L3) and saved
     settings.
   - On-screen messages (`36263a0`), using a built-in 5x7 font I wrote for this, so
     there's no font asset.
   - Frame time: 1.9 ms median and 2.3 ms at p95, at 1706x1120 on this Ally's Radeon
     780M.
   - Tests: the full ROM suite passes on the `hd2d` build, 67/67, including all
     `diff_all_*` shards (`sync_determinism` is skipped by design).
4. **Engine and game-module split** (`8077c70`, `edebf23`), documented in
   `docs/MODULES.md`:
   - The engine reads its function tables through pointers. A game module (`ct_game.so`)
     installs them at load time.
   - `ct_boot_engine` and `ct_sdl_engine` contain none of the generated game code: about
     1.1 MB, against 42 MB for the static build.
   - `tools/build_module.py` builds the module from your ROM without CMake, in about
     40 s with `-march=native`, then deletes the ROM-derived C.
   - A new `module_parity` ctest checks that the engine plus the module matches the
     static build frame for frame. I also verified all 9,200 frames, which match the
     original upstream baseline too.
   - Without a module, interpreter-only runs at about 290 fps headless, so it's a usable
     fallback.
5. **First run and package check** (branch `first-run`), documented in
   `docs/MODULES.md`:
   - `ct_sdl_engine` without `--module` finds the ROM (`--rom`, `$CT_ROM`, the last
     one used, or a file dropped on the window) and checks its SHA-1 against
     `game.toml`. It builds the module with `build_module.py` behind a progress
     screen, then plays. A stamp next to the module (ROM SHA-1 and module ABI)
     triggers a rebuild when either changes. If the build fails, a key press plays
     in the interpreter.
   - `tools/check_dist.py` scans a package, archives included, for module or
     generated-source names, an exported `ct_module_get`, generated function names,
     and sampled 64-byte ROM windows. The `check_dist` ctest covers it.

### Things that need your decision
- **krtp0028 fork merge:** the permission check blocked building and running this
  unreviewed third-party code while you were away, so I aborted the merge. Its Windows
  and PSP work includes the `ucontext` compatibility layer that the Android port needs.
  Review it with `git log a6b4925..krtp/main` (the `krtp` remote is set up). Tell me if
  it's OK to merge.

### Mistake caught and fixed
- The first SDL commit accidentally included the `build-hd/` folder, which contained
  binaries built from ROM-derived code.
- I amended that unpushed commit, added `build-*/` to `.gitignore`, and pruned the
  unreachable objects (`git gc --prune=now`).
- I checked every commit on every branch: none contains build output or ROM data.
- I now stage explicit paths and review them before committing.

### Next, in plan order
1. **AppImage and Flatpak** of the engine, bundling Python and a compiler for
   `build_module.py`, with `CT_ENGINE_DATA_DIR` pointing at the bundled files, and
   checked with `tools/check_dist.py`.
2. **HD-2D, next level:** per-layer PPU output (not just the winning pixel), for real
   parallax and camera tilt.
3. **Android:** needs the fork merge.
