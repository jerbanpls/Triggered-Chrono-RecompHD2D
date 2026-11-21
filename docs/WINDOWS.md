<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Windows (experimental)

**Status: experimental.** The Windows package has been tested under Wine and in a
CI run on a Windows runner (a synthetic module built with the package's own tools).
Nobody has yet played it on real hardware for long. HD-2D mode and game controllers
have not been tested on Windows at all. Expect rough edges.

## What the package is

`CT_Recomp-win64.zip` unpacks to one folder, `CT_Recomp\`, about 300 MB (the zip is
about 85 MB). Like the Linux packages it holds the engine and **nothing from the
game**: no ROM, no generated code, no module. You bring your own legally obtained ROM
and the game is built on your PC the first time you start it.

```
CT_Recomp\
  ct_sdl_engine.exe        the game (a normal window; double-click it)
  ct_boot_engine.exe       command-line test tool (not needed to play)
  SDL2.dll                 SDL 2.32 (zlib licence)
  libct_*_engine.dll.a     import libraries the first-run module build links against
  ct_engine.def            the list of engine functions those were made from
  python\                  embeddable Python 3.13 (runs tools\build_module.py)
  zig\                     zig 0.15.2, trimmed to compiling C for x86_64-windows-gnu
  tools\  recomp\  runtime\  game\ct\    source files the module build reads
  LICENSE  LICENSES\  THIRD_PARTY.md  CREDITS.md  README-WINDOWS.txt
```

No installer, no registry entries, no admin rights. You need a 64-bit Windows 10 or 11.
The programs are not code-signed, so SmartScreen or an antivirus may warn.

## Starting it

1. Unzip the whole folder somewhere you can write to (not `Program Files`: the
   engine keeps nothing there, but a read-only folder is a bad place for a tool folder).
2. Double-click `ct_sdl_engine.exe`.
3. It looks for your ROM in `Documents`, `Downloads` and its own folder (the package's
   own subfolders are skipped), plus anything you used before. If it finds none, drag
   the ROM file onto the window. `--rom PATH` and the `CT_ROM` variable also work.

### First launch builds the game

The first launch (and any launch after the ROM or the engine changed) translates your
ROM into C and compiles it with the bundled Python and zig into `ct_game.dll`. The
very first compile also builds zig's mingw-w64 runtime, so allow a few minutes
(about 40 s with a warm cache under Wine on a desktop CPU; real PCs vary). Later
launches load the finished module at once. Nothing has to be installed, and the build
does not use any compiler or Python that happens to be on your PC unless you point
the engine at it: `CT_PYTHON` and `CT_ZIG` name other copies, `CT_ROM` names a ROM.

## Where files go

| What | Where |
| --- | --- |
| Saves, settings, the built module, quick-menu state | `%APPDATA%\ct-recomp\ct\` |
| Log (when started from Explorer; with a console or `> file` redirect it prints there instead) | `%APPDATA%\ct-recomp\ct\ct_sdl.log` |
| zig's compile cache | zig's default (`%LOCALAPPDATA%\zig`) |

Delete `%APPDATA%\ct-recomp\ct\` to start fresh (your saves are in it).

## Known limits

- **Experimental.** Tested under Wine (the ROM-free test set, the module build, and a
  600-frame replay whose frame hashes match the Linux run) and in CI (the unmodified
  `zig.exe` building a synthetic module). Not yet tested: long play sessions, Explorer
  start behaviour and drag-and-drop on a real desktop, HD-2D mode, controllers and
  audio devices on Windows.
- The module is linked against the engine program by name: a module built for
  `ct_sdl_engine.exe` loads only in that program.
- Generated modules are built with `-march=native` like on Linux, so they are tied to
  the CPU they were built on (the first launch builds one per PC anyway).
- A rejected ROM is remembered by size and time, not by file identity (Windows has no
  inode numbers): replacing a rejected ROM by one with the same size and time would
  be skipped until the cache is deleted. Rare edge case.

Building the package: see the Windows section of [PACKAGING.md](PACKAGING.md).
