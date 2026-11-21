SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later

CT_Recomp for Windows (EXPERIMENTAL)
====================================

This is an experimental test package of a native PC port of Chrono Trigger
(static recompilation plus an optional HD-2D look). It has been tested under
Wine and in a CI run on Windows, not yet on many real machines. Expect rough
edges, and please report problems.

What you need
  * A 64-bit Windows 10 or 11 PC.
  * Your own legally obtained Chrono Trigger (SNES) ROM file. The game itself
    is NOT included and never will be.

How to start
  1. Unzip the whole CT_Recomp folder anywhere you can write to (not into
     "Program Files").
  2. Double-click ct_sdl_engine.exe.
  3. The first time, it looks for your ROM in Documents, Downloads and this
     folder. If it finds none, drag the ROM file onto the window.
  4. The first launch builds the game for your PC from your ROM (this takes
     a few minutes; later launches are instant). The tools it needs (Python
     and a C compiler) are in the python and zig folders; nothing has to be
     installed.

Where things go
  Saves, settings, the built game and the log (ct_sdl.log) are in
  %APPDATA%\ct-recomp\ct. Delete that folder to start fresh.

Notes
  * Windows SmartScreen or your antivirus may warn about an unsigned program
    it has not seen before. The package is not code-signed.
  * ct_boot_engine.exe is a command-line test tool; you do not need it.
  * Licences: see LICENSE, LICENSES\, THIRD_PARTY.md and CREDITS.md.
    Python (python\LICENSE.txt), zig (zig\LICENSE) and SDL2 (LICENSES\SDL2.txt)
    keep their own licences.
