@echo off
rem SPDX-FileCopyrightText: 2026 jerbanpls
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Runs inside the package (C:\pkg), under Wine, by test-wine.sh OUT rom ROMFILE.
rem PATH holds the package folder and the system folders only: the bundled
rem Python and zig are found by the engine from its own folder.
rem C:\wzig holds the package's zig.exe patched for Wine (packaging/windows/wine-zig),
rem named by CT_ZIG; C:\rom\ct.sfc is the user's ROM (copied into C:\work here).
set PATH=C:\pkg;C:\windows\system32;C:\windows
set CT_ZIG=C:\wzig\zig.exe
set ZIG_LIB_DIR=C:\pkg\zig\lib
set ZIG_GLOBAL_CACHE_DIR=C:\zcache
set ZIG_LOCAL_CACHE_DIR=C:\zcache
cd /d C:\pkg
mkdir C:\work
copy /y C:\rom\ct.sfc C:\work\rom.sfc >nul
echo === (a) ct_sdl_engine.exe: first run builds the module from the ROM, then 600 frames
ct_sdl_engine.exe --rom C:\work\rom.sfc --fast --frames 600 --script C:\replay\leene_square.txt
if errorlevel 1 (echo ct_sdl_engine.exe failed: %errorlevel% & exit /b 1)
dir "%APPDATA%\ct-recomp\ct"
echo === (b) a module for ct_boot_engine.exe from the same sources, no -march=native
C:\pkg\python\python.exe tools\build_module.py --game game\ct --rom C:\work\rom.sfc --out C:\work\mod --cc "\"C:\wzig\zig.exe\" cc -target x86_64-windows-gnu" --engine-lib C:\pkg\libct_boot_engine.dll.a
if errorlevel 1 (echo build_module.py failed & exit /b 1)
set CT_ROM=C:\work\rom.sfc
ct_boot_engine.exe --module C:\work\mod\ct_game.dll --script C:\replay\leene_square.txt --frames 600 --hash-log C:\out\win-hash.log
echo ct_boot_engine exit: %errorlevel%
exit /b %errorlevel%
