@echo off
rem SPDX-FileCopyrightText: 2026 jerbanpls
rem SPDX-License-Identifier: GPL-3.0-or-later
rem
rem Real-Windows check (CI job windows-package-test): %1 is the unpacked
rem package folder (CT_Recomp), %2 the folder holding these synthetic sources.
rem Builds the synthetic module with the package's own python.exe and the
rem UNMODIFIED zig.exe (no ZIG_LIB_DIR, default cache), then loads it with
rem the package's ct_boot_engine.exe. Prints WIN-MODULE-OK like the Wine test.
setlocal
set PKG=%~1
set SRC=%~2
set WORK=%RUNNER_TEMP%
if "%WORK%"=="" set WORK=%TEMP%
set WORK=%WORK%\ct-synth-test
rmdir /s /q "%WORK%" 2>nul
mkdir "%WORK%"
set ZIG_LIB_DIR=
set CT_ZIG=
set CT_PYTHON=
set PATH=%PKG%\python;%SystemRoot%\system32;%SystemRoot%
cd /d "%PKG%"
echo python is: & where python
echo zig version:
"%PKG%\zig\zig.exe" version
if errorlevel 1 (echo zig.exe does not run & exit /b 1)
python tools\build_module.py --synth "%SRC%" --out "%WORK%\mod" --cc "\"%PKG%\zig\zig.exe\" cc -target x86_64-windows-gnu" --engine-lib "%PKG%\libct_boot_engine.dll.a"
if errorlevel 1 (echo build_module.py failed & exit /b 1)
dir "%WORK%\mod"
rem ct_boot wants a ROM file before it loads a module: 4 MiB of zeros (not a game)
python -c "import sys; open(sys.argv[1],'wb').write(bytes(0x400000))" "%WORK%\blank.bin"
set CT_ROM=%WORK%\blank.bin
"%PKG%\ct_boot_engine.exe" --module "%WORK%\mod\ct_synth.dll" --frames 0 > "%WORK%\boot.log" 2>&1
set RC=%errorlevel%
type "%WORK%\boot.log"
if not %RC%==0 (echo ct_boot_engine exit %RC% & exit /b 1)
findstr /r /c:"ct_boot: game module .*ct_synth.dll: 2 functions" "%WORK%\boot.log" >nul
if errorlevel 1 (echo module not reported & exit /b 1)
echo WIN-MODULE-OK
exit /b 0
