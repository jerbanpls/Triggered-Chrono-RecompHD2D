#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross-build the engine for 64-bit Windows with zig (EXPERIMENTAL package).
#
#   packaging/windows/build.sh [OUT]
#
# OUT (default: build-win64/ in the repo, git-ignored) gets downloads/, the
# CMake build, stage/ (the unpacked package) and CT_Recomp-win64.zip.
# Runs on Linux; without cmake on PATH it re-runs itself in the `ct-build`
# distrobox. Later leaves add Python, zig for Windows, tools and a launcher.
set -eu

repo=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "${1:-$repo/build-win64}"
out=$(cd "${1:-$repo/build-win64}" && pwd)

if ! command -v cmake >/dev/null 2>&1; then
    command -v distrobox >/dev/null 2>&1 || { echo "build.sh: need cmake (or distrobox with ct-build)" >&2; exit 1; }
    exec distrobox enter ct-build -- sh "$repo/packaging/windows/build.sh" "$out"
fi

# ---- pinned downloads: URL, SHA-256 ----
ZIG_URL=https://ziglang.org/download/0.15.2/zig-x86_64-linux-0.15.2.tar.xz
ZIG_SHA=02aa270f183da276e5b5920b1dac44a63f1a49e55050ebde3aecc9eb82f93239
SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
ZIGWIN_URL=https://ziglang.org/download/0.15.2/zig-x86_64-windows-0.15.2.zip
ZIGWIN_SHA=3a0ed1e8799a2f8ce2a6e6290a9ff22e6906f8227865911fb7ddedc3cc14cb0c
# newest 3.13 patch with an embeddable zip that is at least two weeks old (2026-11-20);
# python.org lists only MD5, so this SHA-256 is the one recorded at first download
PY_URL=https://www.python.org/ftp/python/3.13.17/python-3.13.17-embed-amd64.zip
PY_SHA=9e35319bffedab8b36ad44d8f377cb4f1733bcec257a0b6d04d94f01486ddb38
SDL_SHA=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2

dl="$out/downloads"
mkdir -p "$dl"
fetch() {   # fetch NAME URL SHA256: download once, verify every time
    f="$dl/$1"
    if [ ! -f "$f" ]; then
        curl -fL --retry 3 -o "$f.part" "$2"
        mv "$f.part" "$f"
    fi
    echo "$3  $f" | sha256sum -c - >/dev/null || {
        echo "build.sh: SHA-256 mismatch for $1" >&2
        rm -f "$f"
        exit 1
    }
}
fetch zig-linux.tar.xz "$ZIG_URL" "$ZIG_SHA"
fetch SDL2-devel-mingw.tar.gz "$SDL_URL" "$SDL_SHA"
fetch zig-windows.zip "$ZIGWIN_URL" "$ZIGWIN_SHA"
fetch python-embed.zip "$PY_URL" "$PY_SHA"

tools="$out/tools"
if [ ! -x "$tools/zig/zig" ]; then
    rm -rf "$tools/zig"; mkdir -p "$tools/zig"
    tar -xJf "$dl/zig-linux.tar.xz" -C "$tools/zig" --strip-components=1
fi
if [ ! -d "$tools/SDL2-2.32.10" ]; then
    tar -xzf "$dl/SDL2-devel-mingw.tar.gz" -C "$tools"
fi
sdl="$tools/SDL2-2.32.10/x86_64-w64-mingw32"
export CT_ZIG="$tools/zig/zig"
# zig's cache inside OUT: nothing outside the build tree is written.
export ZIG_GLOBAL_CACHE_DIR="$out/zig-cache" ZIG_LOCAL_CACHE_DIR="$out/zig-cache"

bld="$out/build"
cmake -S "$repo" -B "$bld" -G "${CMAKE_GENERATOR:-Unix Makefiles}" \
    -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/zig-windows.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCT_ENGINE_ONLY=ON -DCT_ENGINE_DATA_DIR:STRING=. \
    -DCMAKE_PREFIX_PATH="$sdl" -DCMAKE_INSTALL_PREFIX="$out/stage"
build() { cmake --build "$bld" -j "${JOBS:-$(nproc)}" --target ct_sdl_engine ct_boot_engine; }
build
# zig's linker exports nothing from an exe by itself: list the engine's
# global symbols (the whole-archive libraries) in a .def file and link again;
# CMake's --out-implib then yields libct_*_engine.dll.a too.
def="$out/ct_engine.def"
{
    echo EXPORTS
    for l in libct_sched.a libct_interp.a libct_runtime.a; do
        nm -g --defined-only "$bld/$l"
    done | awk '$2 ~ /^[TDBRC]$/ && $3 !~ /^[._]/ && $3 != "main" {
        print "    " $3 ($2 == "T" ? "" : " DATA") }' | sort -u
} > "$def"
echo "exported symbols: $(($(wc -l < "$def") - 1))"
cmake -S "$repo" -B "$bld" -DCT_EXE_DEF="$def"
rm -f "$bld/ct_sdl_engine.exe" "$bld/ct_boot_engine.exe"
build

# The Windows package layout is flat (not the Linux install tree): programs,
# SDL2.dll and the import libraries next to each other; licences in LICENSES/.
rm -rf "$out/stage"; mkdir -p "$out/stage/LICENSES"
cp "$bld/ct_sdl_engine.exe" "$bld/ct_boot_engine.exe" "$sdl/bin/SDL2.dll" "$out/stage/"
cp "$bld/libct_sdl_engine.dll.a" "$bld/libct_boot_engine.dll.a" "$out/stage/"
cp "$repo"/LICENSES/*.txt "$out/stage/LICENSES/"
cp "$sdl/../LICENSE.txt" "$out/stage/LICENSES/SDL2.txt"
cp "$repo/LICENSE" "$repo/THIRD_PARTY.md" "$repo/packaging/windows/CREDITS.md" "$out/stage/"
# the user-facing text without its REUSE header lines, CRLF for Notepad
grep -v '^SPDX-' "$repo/packaging/windows/README-WINDOWS.txt" | sed '1{/^$/d}' | sed 's/$/\r/' \
    > "$out/stage/README-WINDOWS.txt"

# The toolchain the package carries, so the player needs nothing installed:
# zig for Windows (trimmed to compiling and linking C for x86_64-windows-gnu)
# and the embeddable Python (build_module.py needs only its standard library).
rm -rf "$out/zigwin-x"
python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' \
    "$dl/zig-windows.zip" "$out/zigwin-x"
sh "$repo/packaging/windows/trim-zig-win.sh" "$out/zigwin-x/zig-x86_64-windows-0.15.2"
mv "$out/zigwin-x/zig-x86_64-windows-0.15.2" "$out/stage/zig"
rmdir "$out/zigwin-x"
mkdir -p "$out/stage/python"
python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' \
    "$dl/python-embed.zip" "$out/stage/python"
# the embeddable build ignores PYTHONPATH and the script folder: the _pth file
# lists what `import` searches (the zip, this folder, the package's tools)
printf 'python313.zip\n.\n../tools\n../recomp\n' > "$out/stage/python/python313._pth"
mkdir -p "$out/stage/tools" "$out/stage/runtime"
cp "$repo/tools/build_module.py" "$out/stage/tools/"
cp "$repo"/runtime/*.h "$repo/runtime/module_entry.c" "$out/stage/runtime/"
# the data folder (the exe's own folder, "."): what build_module.py reads, as
# the Linux install rules do: tools, the translator and each game's configuration
mkdir -p "$out/stage/recomp"
cp "$repo"/tools/*.py "$out/stage/tools/"
cp "$repo"/recomp/*.py "$out/stage/recomp/"
for g in "$repo"/game/*/; do
    [ -f "$g/game.toml" ] || continue
    gid=$(basename "$g"); mkdir -p "$out/stage/game/$gid/tools"
    cp "$g"/*.toml "$g"/*.c "$g"/*.h "$out/stage/game/$gid/" 2>/dev/null || true
    cp "$g"/tools/*.py "$out/stage/game/$gid/tools/" 2>/dev/null || true
done
cp "$out/ct_engine.def" "$out/stage/"   # the exports the import libraries were made from

# Nothing from running the tools in the stage folder ships.
find "$out/stage" -name __pycache__ -type d -prune -exec rm -rf {} +
find "$out/stage" -name '*.pyc' -delete

# The zip: top folder CT_Recomp/, files in sorted order, fixed timestamps and
# permissions, fixed compression level, so the same inputs give the same bytes
# (given the same zlib: the exes themselves are deterministic only as far as
# zig's linker is; see docs/PACKAGING.md).
rm -f "$out/CT_Recomp-win64.zip"
python3 - "$out/stage" "$out/CT_Recomp-win64.zip" <<'PY'
import os, sys, zipfile
src, dst = sys.argv[1:3]
names = []
for root, _, files in os.walk(src):
    for f in files:
        p = os.path.join(root, f)
        names.append(os.path.relpath(p, src).replace(os.sep, "/"))
names.sort()
with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for n in names:
        zi = zipfile.ZipInfo("CT_Recomp/" + n, (2026, 1, 1, 0, 0, 0))
        zi.compress_type = zipfile.ZIP_DEFLATED
        zi.create_system = 3
        zi.external_attr = 0o100644 << 16
        with open(os.path.join(src, n), "rb") as fh:
            z.writestr(zi, fh.read(), compresslevel=9)
PY
ls -l "$out/stage"
du -sm "$out/stage" | awk '{print "unpacked MB: " $1}'
ls -l "$out/CT_Recomp-win64.zip" | awk '{print "zip bytes: " $5}'
sha256sum "$out/CT_Recomp-win64.zip"
echo WIN-BUILD-OK
