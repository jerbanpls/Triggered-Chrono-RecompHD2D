@echo off
rem SPDX-FileCopyrightText: 2026 jerbanpls
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Runs inside the package (C:\pkg), under Wine, by test-wine.sh OUT module:
rem builds the synthetic module with the package's own Python and zig (PATH
rem holds nothing else) and loads it with the engine.
rem C:\wzig holds the package's zig.exe patched for Wine (packaging/windows/wine-zig)
set PATH=C:\pkg\python;C:\wzig;C:\windows\system32;C:\windows
set ZIG_LIB_DIR=C:\pkg\zig\lib
rem C:\zcache is a folder kept by test-wine.sh between runs (see there)
set ZIG_GLOBAL_CACHE_DIR=C:\zcache
set ZIG_LOCAL_CACHE_DIR=C:\zcache
cd /d C:\pkg
echo python is: & where python
echo zig is: & where zig
python tools\build_module.py --synth C:\pkg\synth --out C:\work\mod --cc "zig cc -target x86_64-windows-gnu" --engine-lib C:\pkg\libct_boot_engine.dll.a
if errorlevel 1 exit /b 1
dir C:\work\mod
rem ct_boot wants a ROM file before it loads a module: a 4 MiB file of zeros
rem (not a game; it cannot run a frame, so --frames 0: load, report, return)
python -c "open(r'C:\work\blank.bin','wb').write(bytes(0x400000))"
set CT_ROM=C:\work\blank.bin
ct_boot_engine.exe --module C:\work\mod\ct_synth.dll --frames 0
exit /b %errorlevel%
