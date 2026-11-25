#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the engine-only AppImage (docs/PACKAGING.md).
#
#   packaging/appimage/build.sh [WORKDIR]
#
# WORKDIR (default: build-appimage/ in the repo) holds the CMake build, the
# downloads, the AppDir and the finished CT_Recomp-x86_64.AppImage.
#
# The image holds the engine built with -DCT_ENGINE_ONLY=ON (no game code),
# its data dir, SDL2, a Python (python-build-standalone) and zig, so that
# tools/build_module.py can build the game module from the user's ROM on
# the user's machine. It never contains a module, a ROM or generated C: the
# script checks the AppDir for them before it packs it.
#
# Environment:
#   SDL2=system   (default) bundle the libSDL2 the engine links against; when
#                 it is sdl2-compat, its libSDL3 too
#   SDL2=build    build SDL 2.32.10 from the pinned release tarball; needs the
#                 X11/Wayland/audio development headers for its backends
#   CMAKE=...     cmake to use (default: cmake on PATH)
#   JOBS=N        parallel build jobs
#
# Build on the oldest distribution you want the image to run on: the engine
# and SDL link against the build host's glibc.
set -eu

repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mkdir -p "${1:-$repo/build-appimage}" && cd "${1:-$repo/build-appimage}" && pwd)
cmake=${CMAKE:-cmake}
# Fail before the downloads, not after them.
command -v "$cmake" >/dev/null 2>&1 || {
    echo "build.sh: cmake not found ('$cmake'): install CMake 3.24+ or set CMAKE=/path/to/cmake" >&2
    exit 2
}
jobs=${JOBS:-$(nproc)}
sdl2=${SDL2:-system}
app_id=io.github.jerbanpls.CtRecomp

# ---- pinned downloads: URL, SHA-256 ----
ZIG_URL=https://ziglang.org/download/0.15.2/zig-x86_64-linux-0.15.2.tar.xz
ZIG_SHA=02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239
PY_URL=https://github.com/astral-sh/python-build-standalone/releases/download/20261013/cpython-3.13.16%2B20261013-x86_64-unknown-linux-gnu-install_only_stripped.tar.gz
PY_SHA=d0080766b6b74aeb81e1949ef0d9a20e6c17f7b160fb295a69b936ccb17660be
TOOL_URL=https://github.com/AppImage/appimagetool/releases/download/1.9.1/appimagetool-x86_64.AppImage
TOOL_SHA=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0
RUNTIME_URL=https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64
RUNTIME_SHA=2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d
SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
SDL_SHA=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165

dl="$work/downloads"
mkdir -p "$dl"
fetch() {   # fetch NAME URL SHA256: download once, verify every time
    f="$dl/$1"
    if [ ! -f "$f" ]; then
        curl -fL --retry 3 -o "$f.part" "$2"
        mv "$f.part" "$f"
    fi
    echo "$3  $f" | sha256sum -c - >/dev/null || {
        echo "build.sh: SHA-256 mismatch for $1" >&2
        exit 1
    }
}
fetch zig.tar.xz "$ZIG_URL" "$ZIG_SHA"
fetch python-stripped.tar.gz "$PY_URL" "$PY_SHA"
fetch appimagetool "$TOOL_URL" "$TOOL_SHA"
fetch runtime-x86_64 "$RUNTIME_URL" "$RUNTIME_SHA"
chmod +x "$dl/appimagetool"

appdir="$work/AppDir"
rm -rf "$appdir"
mkdir -p "$appdir/usr/lib"

# ---- Python 3.13 (python-build-standalone, stripped) for tools/build_module.py ----
mkdir -p "$appdir/usr/lib/python"
tar -xzf "$dl/python-stripped.tar.gz" -C "$appdir/usr/lib/python" --strip-components=1
pyroot="$appdir/usr/lib/python"
pylib=$(echo "$pyroot"/lib/python3.*)
# No Tk, IDLE, pip or test suite: build_module.py needs the standard library.
rm -rf "$pylib/test" "$pylib/idlelib" "$pylib/tkinter" "$pylib/turtledemo" \
    "$pylib/ensurepip" "$pylib/lib2to3" "$pylib"/site-packages/pip* \
    "$pyroot"/lib/libtcl* "$pyroot"/lib/libtk* "$pyroot"/lib/tcl* "$pyroot"/lib/tk* \
    "$pyroot"/lib/itcl* "$pyroot"/lib/thread* "$pyroot/include" "$pyroot/share" \
    "$pyroot"/bin/idle* "$pyroot"/bin/pip*
"$pyroot/bin/python3" -c 'import tomllib, concurrent.futures'

# ---- SDL2 built from source (SDL2=build) ----
sdl_args=
if [ "$sdl2" = build ]; then
    fetch SDL2.tar.gz "$SDL_URL" "$SDL_SHA"
    rm -rf "$work/sdl2-src" "$work/sdl2-build"
    mkdir -p "$work/sdl2-src"
    tar -xzf "$dl/SDL2.tar.gz" -C "$work/sdl2-src" --strip-components=1
    "$cmake" -S "$work/sdl2-src" -B "$work/sdl2-build" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$work/sdl2-prefix" -DSDL_STATIC=OFF -DSDL_TEST=OFF
    "$cmake" --build "$work/sdl2-build" -j "$jobs"
    "$cmake" --install "$work/sdl2-build"
    sdl_args="-DCMAKE_PREFIX_PATH=$work/sdl2-prefix -DCT_SDL2_SYSTEM_LIB=OFF"
fi

# ---- the engine: CT_ENGINE_ONLY, so no game code whatever CT_ROM says ----
# shellcheck disable=SC2086
env -u CT_ROM "$cmake" -S "$repo" -B "$work/engine" -DCT_ENGINE_ONLY=ON \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    '-DCMAKE_INSTALL_RPATH=$ORIGIN/../lib' "-DPython3_EXECUTABLE=$pyroot/bin/python3" $sdl_args
"$cmake" --build "$work/engine" -j "$jobs" --target ct_sdl_engine
DESTDIR="$appdir" "$cmake" --install "$work/engine"

engine="$appdir/usr/bin/ct_sdl_engine"
if nm "$engine" | grep -qE ' f_[0-9A-Fa-f]{6}'; then
    echo "build.sh: $engine contains generated functions" >&2
    exit 1
fi

# ---- SDL2 into usr/lib ----
if [ "$sdl2" = build ]; then
    cp -L "$work/sdl2-prefix/lib/libSDL2-2.0.so.0" "$appdir/usr/lib/"
else
    lib=$(ldd "$engine" | awk '$1 == "libSDL2-2.0.so.0" { print $3 }')
    [ -n "$lib" ] || { echo "build.sh: no libSDL2-2.0.so.0 for the engine" >&2; exit 1; }
    cp -L "$lib" "$appdir/usr/lib/libSDL2-2.0.so.0"
    # sdl2-compat (Fedora 42+, Arch, ...) is SDL2's API over SDL3, which it
    # loads at run time.
    if grep -qa 'libSDL3.so.0' "$lib"; then
        sdl3=$(ldconfig -p | awk '$1 == "libSDL3.so.0" { print $NF; exit }')
        [ -n "$sdl3" ] || { echo "build.sh: sdl2-compat without libSDL3.so.0" >&2; exit 1; }
        cp -L "$sdl3" "$appdir/usr/lib/libSDL3.so.0"
    fi
fi

find "$pyroot" -name __pycache__ -prune -exec rm -rf {} +

# ---- zig, run as zig cc by libexec/ct-recomp/zig-cc ----
mkdir -p "$appdir/usr/lib/zig"
tar -xJf "$dl/zig.tar.xz" -C "$appdir/usr/lib/zig" --strip-components=1
sh "$repo/packaging/trim-zig.sh" "$appdir/usr/lib/zig"

# ---- AppDir top level ----
cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/usr/bin/ct-recomp" "$@"
EOF
chmod +x "$appdir/AppRun"
cp "$appdir/usr/share/applications/$app_id.desktop" "$appdir/"
cp "$appdir/usr/share/icons/hicolor/scalable/apps/$app_id.svg" "$appdir/"
ln -sf "$app_id.svg" "$appdir/.DirIcon"

# ---- compliance: nothing built from a ROM ----
bad=$(find "$appdir" \( -name 'ct_game*' -o -iname '*.sfc' -o -iname '*.smc' \
    -o -name 'ct_funcs.*' -o -name 'ct_overlays.c' -o -name 'ct_sdl' -o -name 'ct_boot' \) -print)
if [ -n "$bad" ]; then
    echo "build.sh: ROM-derived files in the AppDir:" >&2
    echo "$bad" >&2
    exit 1
fi

# ---- pack: the pinned runtime, and no FUSE needed to run appimagetool ----
out="$work/CT_Recomp-x86_64.AppImage"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$dl/appimagetool" --no-appstream \
    --runtime-file "$dl/runtime-x86_64" "$appdir" "$out"
echo "build.sh: $out"
