<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Packaging

The packages contain the engine and nothing from the game. The game code is built on
the user's machine, from the user's own ROM, the first time they use it (see
[MODULES.md](MODULES.md)). This page covers how to build the packages and what keeps
game code out of them.

## What a package contains

Everything comes from one CMake install of an engine-only build:

```
bin/ct_sdl_engine                    the engine (no generated code)
bin/ct-recomp                        launcher (packaging/ct-recomp.sh)
libexec/ct-recomp/zig-cc             runs the bundled zig as a C compiler
share/ct-recomp/                     read-only data for tools/build_module.py:
    tools/*.py  recomp/*.py
    runtime/*.h  runtime/module_entry.c
    game/<id>/*.toml  game/<id>/*.c  game/<id>/tools/*.py
share/applications/io.github.jerbanpls.CtRecomp.desktop
share/icons/hicolor/scalable/apps/io.github.jerbanpls.CtRecomp.svg
share/icons/hicolor/{16,24,32,48,64,128,256,512}x*/apps/io.github.jerbanpls.CtRecomp.png
share/metainfo/io.github.jerbanpls.CtRecomp.metainfo.xml
share/licenses/ct-recomp/            LICENSE, LICENSES/, THIRD_PARTY.md
```

The package also bundles what the module build needs at run time:
- **A C compiler:** zig 0.15.2 (MIT), from the official tarball, pinned by SHA-256.
  It runs as `zig cc`, trimmed to its x86_64 Linux libc files by `packaging/trim-zig.sh`.
- **Python 3.11 or later:** the Flatpak uses the runtime's `python3`, which is 3.13 in
  org.freedesktop.Platform 25.08. The AppImage bundles python-build-standalone 3.13.16,
  pinned by SHA-256.

The data dir holds source files from this repository: symbol tables, hooks and the
translator. None of it is derived from a ROM.

## The icon

The app icon is original artwork (an abstract time-ring around a small pixel
diorama; no game imagery), GPL-3.0-or-later. `packaging/icons/make_icons.py`
draws it: it writes `packaging/icons/ct-recomp.svg` and the PNGs
(16 to 512 px, `packaging/icons/NxN/ct-recomp.png`), using rsvg-convert or
ImageMagick. The outputs are committed, so a package build needs neither.
CMake installs them to the hicolor theme under the app id
(`io.github.jerbanpls.CtRecomp`, matching `Icon=` in the .desktop file); the
Flatpak gets them through the same install, and the AppImage uses the SVG as
the AppDir icon. After changing the script, rerun it and commit the output.

## The engine-only build

```
cmake -S . -B build-engine -DCT_ENGINE_ONLY=ON -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build-engine -j
DESTDIR=$PWD/stage cmake --install build-engine
```

`CT_ENGINE_ONLY=ON` does three things:
- It never adds `game/<id>/CMakeLists.txt`, even when `$CT_ROM` is set, so no code is
  generated and neither `ct_game.so` nor the static `ct_sdl` or `ct_boot` is built.
- It builds the engine-only programs `ct_sdl_engine` and `ct_boot_engine`. These now
  live in the top-level CMakeLists.txt, and game builds use them too.
- It fails at configure time if SDL2 is missing, rather than quietly building no
  frontend.

The `install()` rules name their files one by one: `ct_sdl_engine`, the launcher and
the data dir. No rule installs a module, a generated file, or anything else from a
build tree.

## Launching

The `.desktop` file runs `ct-recomp`. The launcher:
1. Puts the bundled Python on `PATH` if there is one. It sets `CC` to the bundled zig
   (`libexec/ct-recomp/zig-cc`) unless `CC` is already set.
2. Starts the engine with `--data-dir <prefix>/share/ct-recomp` (and `--game $CT_RECOMP_GAME`
   if that is set). The engine's own first-run flow does the rest: it finds the ROM,
   builds the module if needed and remembers the ROM's path.

To build the module from the command line without starting the game:

```
ct-recomp --build-module /path/to/your/rom.sfc
```

This runs `ct_sdl_engine --rom ROM --build-only`: the same first-run flow, without a
window. It checks the ROM, builds the module with `tools/build_module.py --native` from
the data dir unless an up-to-date one is there, loads it once to check it, and exits 0,
or 1 with the reason on stderr. Everything goes to the engine's per-game folder,
`$XDG_DATA_HOME/ct-recomp/<game id>/` (in the Flatpak,
`~/.var/app/io.github.jerbanpls.CtRecomp/data/ct-recomp/<game id>/`): `ct_game.so`,
`ct_game.stamp` (the ROM's SHA-1 and the module ABI), and `rom_path`, which the next
normal start uses. The in-game saves (`<game id>.srm`) live there too. Nothing is
written to the data dir, which may be read-only.

Earlier launchers built the module into `$XDG_DATA_HOME/ct-recomp/ct_game.so` and kept
the ROM's path in `rom.path` there. Neither file is used any more and both can be
deleted.

## Flatpak

`packaging/flatpak/io.github.jerbanpls.CtRecomp.yml` uses org.freedesktop.Platform
25.08. You need, at user or system level, `org.freedesktop.Platform//25.08`,
`org.freedesktop.Sdk//25.08` and either a `flatpak-builder` on `PATH` or the
`org.flatpak.Builder` app. No SDK extensions are needed.

Build it with the wrapper, from anywhere in the checkout:

```
packaging/flatpak/build.sh
```

With no arguments this runs
`flatpak-builder --user --force-clean --repo=build-flatpak-repo build-flatpak <manifest>`
(or `flatpak run org.flatpak.Builder ...` if there is no `flatpak-builder`). Arguments
replace those defaults, for example `packaging/flatpak/build.sh --user --install
--force-clean build-flatpak`. The build dir, the repo and `.flatpak-builder/` are all
git-ignored.

Install it from the local repo, then run it:

```
flatpak --user remote-add --no-gpg-verify ctrecomp-local build-flatpak-repo
flatpak --user install ctrecomp-local io.github.jerbanpls.CtRecomp
flatpak run io.github.jerbanpls.CtRecomp --rom ~/Downloads/your_dump.sfc
```

The first run builds the module inside the sandbox, with the runtime's `python3` and
the bundled zig, into `~/.var/app/io.github.jerbanpls.CtRecomp/data/ct-recomp/<id>/`.
Later runs reuse it and remember the ROM, so `--rom` can be left out.
`ct-recomp --build-module ROM` does the same build, then quits after one frame.

To make a single-file bundle: `flatpak build-bundle build-flatpak-repo
CtRecomp.flatpak io.github.jerbanpls.CtRecomp`.

The manifest has three modules:
- SDL2 2.32.10, built from the release tarball (pinned by SHA-256) out of tree, with
  the Wayland (libdecor), X11, KMSDRM, PipeWire, PulseAudio, ALSA, udev and HIDAPI
  backends from the SDK. The runtime also ships sdl2-compat, but `/app/lib` comes first.
- zig, installed in `/app/lib/zig` and trimmed to x86_64 Linux.
- The engine, built with `CT_ENGINE_ONLY=ON`, `CT_SDL2_SYSTEM_LIB=OFF` and
  `CT_ENGINE_DATA_DIR=/app/share/ct-recomp`.

The engine's source is `git archive HEAD`, which `build.sh` unpacks into
`build-flatpak-src/ct-recomp` for the manifest's `dir` source. So only **committed**
files go into the build: never `rom/`, a build tree, or uncommitted changes (`build.sh`
warns if there are any). Commit before you build. A `git` source can't do this. It
mirrors the whole repository and checks out a named branch (`master` by default), not
the checkout's HEAD, so it failed in a worktree. A local `archive` source can't either,
since flatpak-builder caches it by its path and reuses a stale engine after a new
commit. The `dir` source makes the engine module rebuild on every run. SDL2 and zig
stay cached.

The engine module's `post-install` step fails the build if `nm` finds a generated
`f_XXXXXX` symbol in `ct_sdl_engine`, or can't read its symbol table. It also fails if
`/app` contains a module, a ROM image, or a generated table or source. To confirm this
step can fail, a test build with an extra `touch /app/share/ct_game.so` before the
checks was run, and it stopped at the engine module.

Permissions: Wayland with X11 as a fallback, IPC, PulseAudio, `--device=all`
(controllers and GPU), and read-only Downloads and Documents for picking the ROM. A ROM
stored anywhere else needs `flatpak override --user --filesystem=<dir>:ro
io.github.jerbanpls.CtRecomp`, or `--filesystem=<dir>:ro` on a single `flatpak run`.

### Test results

Tested on 2026-11-13 on Bazzite (Fedora 45 base, kernel 7.3.3), Ryzen Z1 Extreme, with
the Platform and Sdk 25.08 and the `org.flatpak.Builder` app. This was a user install
from the local repo, run headless with `--env=SDL_VIDEODRIVER=offscreen
--env=SDL_AUDIODRIVER=dummy` and `--filesystem=<rom dir>:ro`:

| Check | Result |
| --- | --- |
| Clean build (SDL2, zig, engine) | passes, post-install checks included |
| First run, `--frames 600 --fast --hd2d --require-render --rom ...` | builds the module in the sandbox (`python3` = /usr/bin/python3 3.13.16, `CC` = /app/libexec/ct-recomp/zig-cc, 16 jobs, 102 to 126 s), loads it (1482 functions), exits 0. 133 s in total. |
| Second run | `ct_game.so: up to date`, exits 0 in about 6 s |
| Run without `--rom` | `ROM (remembered)`, exits 0 |
| `--build-module ROM` from an empty data dir | builds into `.../data/ct-recomp/ct/`, exits 0 |
| `--shot` | 1920x1080 PNG of the title screen |
| GL in the sandbox | `hd2d: AMD / AMD Ryzen Z1 Extreme (radeonsi, phoenix, ACO, DRM 3.64, ...) / 4.6 (Core Profile) Mesa 26.2.5`. That is hardware radeonsi through the runtime's Mesa (GL.default 26.2.5), not llvmpipe. |
| Bundle size | 45,652,624 bytes (44 MiB). The installed app is 245 MiB, of which zig is 242 MiB. |
| `check_dist.py --rom` on `build-flatpak/files`, the OSTree export and the bundle's imported contents | At first 1 hit, a false positive in `lib/zig/zig` (see below). Since check_dist skips generic tables: 8,024 files, 0 problems. |

The first run's single hit was in the unmodified upstream zig 0.15.2 binary,
byte-identical to the one in the SHA-256-pinned tarball. At file offset 34131568 it
holds the standard 256-byte bit-reversal table (`00 80 40 c0 20 a0 60 e0 ...`). The ROM
has the same table at 0xFD00, so the ROM-window check matched it. Nothing from the ROM
was in the package.

`check_dist` now never samples a ROM window that lies inside a table that is part of no
game's data: bit reversal, identity, and reversed identity. The same table also turns
up in the AppImage's squashfs, where a per-file rule can't catch it. With that,
packages pass without exemptions:

```
python3 tools/check_dist.py PACKAGE --rom ROM
```

It also fails any asset pack (docs/ASSET-PACKS.md): a `pack.toml`, or a picture
named by a content key under `tiles/`, `sprites/` or `objects/`. Packs are made from the
user's own ROM and live in their data folder; they never go into a package.

`--allow packaging/check_dist_allow.txt` remains for an unmodified, pinned upstream
binary that matches some other ROM window by chance. Listed files skip the ROM-window
check only; the name, ELF-export and generated-name checks still apply. It lists the
zig binary. When zig's pin changes, update its hash there.

A second pass on 2026-11-13, from a clean `build.sh --user --install`, with the
`--hd2d --frames 600 --log-pacing` run (offscreen video, dummy audio, `--filesystem=<rom dir>:ro`):
- The first run built the module in the sandbox (43 s), loaded 1,482 functions and ran
  600 frames at 60.10 Hz, 0 frames over 25 ms, 0 audio underruns, exit 0. GL was
  hardware radeonsi (Mesa 26.2.5, core 4.6).
- The data dir `~/.var/app/io.github.jerbanpls.CtRecomp/data/ct-recomp/ct/` then held
  `ct_game.so`, `ct_game.stamp` and `rom_path`. A save appears only once the game's
  SRAM changes: a run with `--poke-sram 100:10=AB` wrote `ct.srm` there (8,192 bytes,
  byte 0x10 = 0xAB), so saves land in the sandbox's data dir.
- `check_dist.py --rom --allow` passed on `build-flatpak/files` (8,035 files), on the
  checked-out OSTree export (7,499 files) and on the `flatpak build-bundle` file
  (45.7 MB): 0 problems each.
- Nothing needed fixing in the manifest or the first-run flow.

Not tested yet: a windowed session on Wayland or X11 inside the sandbox, audio through
PulseAudio or PipeWire, controllers in the sandbox, a system-wide install, Flathub's
linter, and aarch64 (the zig source is x86_64 only).

### Desktop file and metainfo

`desktop-file-validate` accepts the `.desktop` file. `appstreamcli validate --no-net`
accepts the metainfo (it had warned about a missing homepage URL; one pedantic note
remains, that the id has upper-case letters, which the app id fixes). Flathub's own
linter was not run.

## AppImage

```
packaging/appimage/build.sh [WORKDIR]      # default WORKDIR: build-appimage/
```

The script:
1. Downloads zig, python-build-standalone, appimagetool 1.9.1 and the type2 runtime
   (release 20251108), and checks each against its pinned SHA-256.
2. Builds the engine with `CT_ENGINE_ONLY=ON` and an `$ORIGIN/../lib` rpath.
3. Installs it into `AppDir/usr`, and adds SDL2, Python and zig.
4. Checks the engine for generated symbols and the AppDir for modules, ROM images and
   generated tables.
5. Packs `CT_Recomp-x86_64.AppImage` with the pinned runtime, which avoids
   appimagetool's own download.

SDL2 comes from one of two places:
- `SDL2=system` (the default) copies the `libSDL2-2.0.so.0` the engine links against.
  When that library is sdl2-compat, as on Fedora 42 and later, it copies the
  `libSDL3.so.0` it loads as well.
- `SDL2=build` builds 2.32.10 from the pinned tarball. This needs the X11, Wayland and
  audio development headers, or SDL ends up without those backends.

The engine and SDL link against the build host's glibc. An image built on Fedora 45
(`SDL2=system`) needs glibc 2.43: `ct_sdl_engine` imports `atan2f@GLIBC_2.43`, and its
bundled `libSDL2` wants `GLIBC_ABI_DT_RELR`, so it failed on Ubuntu 22.04 with
"version `GLIBC_2.43' not found". For an image that runs on older systems, use

```
packaging/appimage/build-container.sh [WORKDIR]    # default: build-appimage-ubuntu2204/
```

It runs `build.sh` with `SDL2=build` in `docker.io/library/ubuntu:22.04` (podman or
docker; `IMAGE=` and `ENGINE=` override), with the checkout mounted read-only. The
result needs **glibc 2.35**: the engine's highest versioned symbol is `GLIBC_2.35` (and
SDL's 2.34), and the bundled Python and zig need only 2.17. CMake 3.24+ comes from
PyPI there; `build.sh` now unpacks its pinned Python first and gives it to CMake, since
22.04's own is 3.10. What the host must provide is what any SDL game needs: libstdc++,
the GL/EGL libraries of its graphics driver, and the sound server. SDL loads X11,
Wayland, PulseAudio, PipeWire and ALSA at run time.

`cmake` must be on PATH, or set `CMAKE=/path/to/cmake`. The script checks for it before
anything is downloaded.

### Test results

These were run on 2026-11-13 on the ROG Ally (Bazzite, Fedora 45 base), building the
`hd2d` branch with the first-run, saves, touch and arm64 work merged:

| Check | Result |
| --- | --- |
| Build (`SDL2=system`) | `CT_Recomp-x86_64.AppImage`, 86,858,232 bytes |
| `check_dist.py --rom` on the image, looking inside its squashfs | 8,093 files, 0 problems, with no `--allow` |
| First launch: `APPIMAGE_EXTRACT_AND_RUN=1`, `PATH=/usr/bin:/bin` (no host cmake or zig), `--rom ROM --hd2d --fast --frames 600 --require-render` | Built the module with the bundled Python and zig (8,033 KiB, 41 s), loaded 1,482 functions, exit 0 |
| Second launch, no `--rom` | Remembered ROM, module "up to date", exit 0 |
| `ct-recomp --build-module ROM` | Module "up to date", exit 0 |
| Saves | No save file without an SRAM change. With `--poke-sram`, written after frame 61, equal to the SRAM dump. Reloaded identically on the next start. |

### Other distributions

The container-built image (86 MB, `check_dist.py --rom --allow`: 8,057 files, 0 problems) was
run headless in podman containers on 2026-11-13: `SDL_VIDEODRIVER=offscreen`,
`SDL_AUDIODRIVER=dummy`, `--appimage-extract-and-run` (no FUSE in containers), the ROM
bind-mounted read-only, an empty data dir, `--rom ROM --hd2d --frames 300`. Each run
built the module from the ROM with the bundled Python and zig (43 to 55 s) and loaded
1,482 functions. The same 300 frames (185 NMIs, PC $7E33D2) came out everywhere.

| Container | glibc | Result |
| --- | --- | --- |
| `ubuntu:22.04` + `libgl1 libegl1 libgl1-mesa-dri` | 2.35 | passes (llvmpipe) |
| `debian:12` + the same | 2.36 | passes (llvmpipe) |
| `fedora:latest` + `mesa-libGL mesa-libEGL mesa-dri-drivers` | 2.44 | passes, 0 underruns |
| `archlinux:latest` (SteamOS-like) + `mesa` | 2.44 | passes (llvmpipe) |
| the earlier Fedora 45-built image on `ubuntu:22.04` | 2.35 | fails: needs glibc 2.43 |

Missing libraries: none in the image itself. A bare container image has no OpenGL
driver, and then the HD-2D run stops with `no usable OpenGL 3.3 / ES 3.0 context`;
installing Mesa fixed that (a desktop has it). The underrun counts in the llvmpipe
runs (50 to 240) are the software renderer and the shared CPU, not the image.
These were not real windows and not real SteamOS: Steam Deck hardware is untested.

Not tested yet: a windowed session of the AppImage on another distribution, real audio,
and controllers.

## NVIDIA and other desktop GPUs

Reviewed in `frontend/hd2d/hd2d.c`, `touch_gl.c` and the shaders for what NVIDIA's
proprietary driver is strict about. Nothing needed changing:
- The context is requested as GL 3.3 core (the driver returns what it has, usually 4.6
  core), with ES 3.0 as the fallback. Shaders start with `#version 330 core`, use
  `layout(location=N) out`, `texture()`/`texelFetch` only, no extensions.
- A VAO is created at init and bound in each frame before the attribute-less
  `glDrawArrays` that a core profile needs (the touch overlay has its own).
- Integer textures (`R8UI`, `usampler2D`) are `GL_NEAREST` with no mipmaps, so they are
  complete. No `sampler2D` and `usampler2D` share a texture unit in any program: every
  sampler is given an explicit unit, which NVIDIA checks at draw time.
- Render targets are `RGBA16F` and `RGBA8`, with a framebuffer completeness check and a
  fall back to `RGBA8` when half-float is not renderable. The multi-target passes use
  `glDrawBuffers`.
- `glGetString(GL_EXTENSIONS)` is only used on ES, where it is legal; a core profile never
  calls it. Unpack and pack alignment are set to 1, for the `R8` and `R8UI` uploads.
- New: `CT_HD2D_GLERR=1` prints (up to 20) GL errors after each frame, which strict drivers
  report and lenient ones hide.

All four looks (`off`, `subtle`, `hd2d`, `deck`) were run on the Ally's radeonsi and on
llvmpipe (`LIBGL_ALWAYS_SOFTWARE=1`, Mesa 26.2.6 core 4.6), 1920x1080 screenshots at
frame 650, with `CT_HD2D_GLERR=1`: no GL errors, and the two renderers' images
differ by at most 1 (`off`), 3 (`subtle`) and 5 (`hd2d`, `deck`) of 255 per channel,
0.06 on average at most. That is rounding, no gross error.

**Not verifiable without NVIDIA hardware:** that the proprietary driver compiles every
shader, links the multi-target programs and reports no error; its Wayland and X11
presentation and vsync (`--pacing auto` picks the timer); and the Flatpak
`org.freedesktop.Platform.GL.nvidia-*` extension. Mesa is the stricter GLSL front end,
so shader errors are unlikely, but the claim is unproven. Intel and non-Mesa AMD drivers
are in the same position.

## The compliance guarantee

No package contains code or data derived from a ROM:
- **Build:** `CT_ENGINE_ONLY` never adds a game directory, so the translator never runs.
- **Install:** only the named engine files and repository sources are installed.
- **Package checks:** the Flatpak's `post-install` and the AppImage script both check
  for generated functions (`nm`, `f_XXXXXX`), modules (`ct_game*`), ROM images and
  generated tables, and fail the build if they find any.
- **Sources:** the Flatpak builds from committed files, and `rom/`, `build*/`, `*.so`
  and `*.AppImage` are git-ignored.

The module the user builds, the ROM path and the saves stay in the user's data dir and
never enter a package.
