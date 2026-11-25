<!--
SPDX-FileCopyrightText: 2026 jerbanpls
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Android build and touch controls

This page covers two things:
- the on-screen touch controller, which works on every platform
- the `android/` Gradle project, which packages the engine as an app

The runtime port to arm64 Android is a separate piece of work, covered in
`docs/ANDROID.md`. **The Android project hasn't been built yet:** this machine
has no Android SDK or NDK. The [Status](#status) section lists what was checked.

## The touch controller

`frontend/touch.c` draws a SNES pad over the picture and turns touches into
pad 1's buttons. The touch buttons combine with the keyboard and the
controller, the same way those two combine with each other.

- **Controls:**
  - a D-pad with 8 directions; you can slide a finger between directions, and
    it keeps working when the finger slides off the pad
  - A, B, X and Y in the SNES diamond; a thumb between two of them presses
    both, and a finger can slide from one button to another
  - L and R
  - Start and Select
- **Multi-touch:** each finger is tracked by its SDL finger id.
- **Size:** the controls are sized in millimetres from the display's DPI, so
  they stay the same size on any screen. They shrink to fit when there isn't
  room. If SDL can't report the DPI, the code assumes a phone (a short side
  of about 68 mm).
- **Layout:**
  - On a wide screen, the controls sit in the side areas beside the picture:
    L, the D-pad and Select on the left; R, the buttons and Start on the right.
  - Otherwise they're anchored to the bottom corners of the screen. On a phone
    held upright that puts them below the picture. On a screen close to 4:3,
    they overlap the picture's bottom corners.
- **Showing and hiding:**
  - On Android the controller is shown from the start. On other systems it
    appears the first time you touch the window.
  - It hides when a game controller is connected or used, and after 15 s with
    no touch (`--touch-hide SECS`, where 0 means never).
  - Touching the screen shows it again. That first touch also presses
    whatever is under your finger.
- **Edit mode:**
  - To enter it, hold a finger on an empty spot for about 0.8 s.
  - Drag a control to move it, or drag the yellow handle at its corner to
    resize it.
  - Hold an empty spot again to save and leave edit mode.
  - The layout is saved to `touch.ini` in SDL's preferences folder
    (`~/.local/share/ct-recomp/touch/` on Linux). Wide and narrow screens are
    saved separately. Delete the file to go back to the default layout.

The plain renderer draws the overlay with `SDL_RenderGeometry`. The HD-2D
renderer draws it with a small separate GL drawer (`frontend/touch_gl.c`, with
its own shader), after `hd2d_render` and before the swap. Every shape is
generated in code, with no image files: circles, rounded rectangles, arrows,
and letters from the 5x7 font in `frontend/hd2d/osd_font.h`.

The plain path now does its own integer scaling (`present_plain` in
`main.c`) and no longer uses `SDL_RenderSetLogicalSize`. With a logical size
set, SDL clamps touch coordinates to the picture, so the side controls could
never be touched. The picture looks the same as before.

### Testing without a touchscreen

| Option | Effect |
| --- | --- |
| `--touch` | Show the overlay from the start; the left mouse button acts as one finger |
| `--touch-hide SECS` | Hide after SECS without a touch (default 15; 0 = never) |
| `--touch-test FILE` | Inject touch events at given frames (below) |

A `--touch-test` file has one event per line:

```
FRAME down|move|up TARGET [FINGER]
```

- `TARGET` is one of two things:
  - a control, which means its centre in the current layout: `a b x y l r start
    select dpad up down left right upleft upright downleft downright`
  - `X,Y`, a point given as fractions of the window (from 0 to 1)
- `FINGER` is a number. It defaults to 0.
- `up` can leave out `TARGET`, and the finger lifts where it is.
- `#` starts a comment.

An event at frame F affects frame F's input, the same as a `--script` line
does. These two files press the same buttons:

```
# script.txt (--script)          # touches.txt (--touch-test)
1500-1505:start                  1500 down start
                                 1506 up
1700-1705:a                      1700 down a 1
                                 1706 up 1
```

While the overlay is shown, `--shot` writes it into the PNG at `--shot-size`
in both renderers. Without the overlay, the plain path still writes the bare
256x224 frame. A screenshot is laid out as a phone screen of that size would be.

`test_touch` (ctest `touch`) checks the input logic without a window:
- D-pad diagonals, sliding, and off-pad directions
- several fingers at once, and sliding between buttons
- the A+B split
- the mouse acting as a finger
- hiding on timeout and when a controller is used
- moving a control in edit mode

## The Android project

```
android/
  settings.gradle, build.gradle, gradle.properties
  app/build.gradle                  SDL fetch, CMake hookup, app id, SDKs
  app/src/main/AndroidManifest.xml
  app/src/main/java/io/github/jerbanpls/ctrecomp/
      ImportActivity.java           launcher: import the ROM and the module
      GameActivity.java             SDLActivity: libSDL2.so + libmain.so
  app/src/main/res/values/strings.xml
```

- App id `io.github.jerbanpls.ctrecomp`, minSdk 26, targetSdk and compileSdk
  35, ABI `arm64-v8a` only.
- Android Gradle plugin 8.7.3 (it needs Gradle 8.9 or later), NDK
  27.2.12479018, and CMake 3.31.6 from the SDK. The root CMakeLists.txt needs
  CMake 3.24 or later for `$<LINK_LIBRARY:WHOLE_ARCHIVE>`, so the SDK's default
  3.22.1 isn't enough. If a different 3.24+ CMake is installed, change
  `externalNativeBuild.cmake.version`.
- The layout follows SDL 2.32.10's `android-project`. No Gradle wrapper is
  committed: build with Android Studio, or run `gradle wrapper
  --gradle-version 8.11.1` once in `android/`.

```
cd android && gradle assembleDebug      # or ./gradlew after creating the wrapper
```

### SDL comes from the release tarball

SDL's Java sources (`org.libsdl.app.*`) aren't copied into this repository. The
`fetchSdl` task in `app/build.gradle` handles them:

1. It downloads
   `https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz`
   into `android/app/build/sdl/`.
2. It checks the file's SHA-256 against
   `5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165`, and fails
   the build if it doesn't match.
3. It unpacks the tarball there.

The app then compiles `SDL2-2.32.10/android-project/app/src/main/java`
alongside its own sources. `preBuild` and the CMake tasks depend on
`fetchSdl`. Everything under `app/build/` is git-ignored.

The same unpacked source is passed to CMake as `CT_SDL2_SOURCE_DIR`, and CMake
builds `libSDL2.so` from it. If that directory doesn't exist yet (for
example, when an IDE sync configures CMake before any Gradle task runs),
the root CMakeLists.txt fetches the same tarball itself with `FetchContent`,
using the same URL and hash.

### Native code: `-DCT_ANDROID=ON`

`externalNativeBuild` points at the repository's root `CMakeLists.txt` with
`-DCT_ENGINE_ONLY=ON -DCT_ANDROID=ON` and builds the `main` and `SDL2` targets.
With `CT_ANDROID` set:

- `CT_ENGINE_ONLY` is forced on. No game directory is added, no code is
  generated, and `ct_game.so`, `ct_sdl` and `ct_boot` aren't built.
- No Python is used. `find_package(Python3)` is skipped, and the HD-2D shaders
  are embedded by `tools/embed_text.cmake`, whose output is identical to
  `tools/embed_text.py`'s apart from the first comment line (checked with
  `diff`).
- SDL2 is built from source as a shared library (`SDL_SHARED=ON`,
  `SDL_STATIC=OFF`, `SDL_TEST=OFF`, `SDL2_DISABLE_INSTALL=ON`).
- `ct_sdl_engine`'s code becomes a **shared library named `main`**
  (`libmain.so`), with the same contents: `frontend/main.c`, the runtime,
  scheduler and interpreter linked whole, the empty function tables, the input
  and touch code, and the HD-2D renderer. SDL.h renames `main` to `SDL_main`,
  and `SDLActivity` calls that.
- The first-run flow (`firstrun.c`) isn't compiled: there's no Python or C
  compiler on a phone to build a module with. The app passes `--rom` and
  `--module` instead.
- CMake processing stops before the tests, the game directories and the
  `install()` rules, so none of them exist in an Android build.

### Importing the ROM and the module

`ImportActivity` is the launcher. It uses the Storage Access Framework
(`ACTION_OPEN_DOCUMENT`), so it needs no storage permission:

- **Pick ROM (.sfc)** copies the chosen file to `files/rom.sfc`.
- **Pick game module (.so)** checks that the file is an ELF64 AArch64 shared
  object and copies it to `files/ct_game.so`. It then marks the file
  read-only, as Android 14+ requires for dynamically loaded code. An
  existing module is deleted and replaced, which works because the files
  dir itself stays writable.
- **HD-2D look** turns on `--hd2d`, which needs OpenGL ES 3.0.
- **Play** is enabled once both files are there. It starts `GameActivity`
  with `--rom <files>/rom.sfc --module <files>/ct_game.so [--hd2d]`.

The module is built on the user's PC from their own ROM. The app contains no
game code and no ROM data. That's the same guarantee as the desktop packages
([PACKAGING.md](PACKAGING.md)).

## Status

**Checked here (Linux host, no Android SDK):**
- Both XML files parse (Python `xml.dom.minidom`).
- The Gradle and Java files have balanced brackets. Their APIs were checked
  by reading them only: there's no JDK or Gradle on this machine, so nothing
  was compiled.
- A host configure with `-DCT_ANDROID=ON` succeeds, using the host
  compiler with no NDK and with `CT_ROM` unset. Its cache has no Python3
  entry, so Python was never looked up.
  - It builds SDL2 from the 2.32.10 source.
  - It creates the `main` and `SDL2` targets, with no `ct_boot_engine`, no
    test targets and no game targets. `CT_ENGINE_ONLY` ends up ON.
  - `ct_hd2d`, `ct_input` and `main.c` compile, and the CMake script embeds
    the shaders.
  - Linking `libmain.so` was not tried, because it needs Android's
    `android` and `log` libraries.

**Not tested:**
- Any Gradle build, the APK, and behaviour on a device: SAF import, the
  read-only module, SDLActivity starting `libmain.so`, touch on real hardware,
  the DPI values Android reports, and GLES 3.0 for the HD-2D drawer.
- **Known blocker, outside this skeleton:**
  - `runtime/sched.c` uses `getcontext`/`makecontext`/`swapcontext`, which
    Bionic doesn't provide. `libmain.so` won't link for Android until the
    runtime port (`docs/ANDROID.md`) replaces them.
  - The module has to be built for arm64.
  - `runtime/module.c` loads it with `dlopen(..., RTLD_LOCAL)`. On Android, a
    library opened with `dlopen` probably can't see `libmain.so`'s symbols
    unless it lists `libmain.so` as a needed library, so the module will
    likely have to be linked against it. This is untested.
- Whether SELinux allows `dlopen` of a file in the app's files dir on current
  Android versions, with the module marked read-only. Emulator frontends that
  download cores do this, but it hasn't been verified for this app.
