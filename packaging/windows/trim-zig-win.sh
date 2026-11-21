#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# trim-zig-win.sh ZIGDIR: shrink an unpacked zig release for Windows (the
# zip) for the package. It only compiles and links C for x86_64-windows-gnu,
# so what stays is the compiler's own library (std, compiler_rt, the clang
# headers) and mingw-w64; the libc sources and headers of every other target,
# the C++ runtimes, sanitizers and docs go. Like packaging/trim-zig.sh.
set -eu
z=$1
rm -rf "$z/doc" "$z/lib/docs" "$z/lib/libcxx" "$z/lib/libcxxabi" "$z/lib/libunwind" \
    "$z/lib/libtsan" "$z/lib/build-web" "$z/lib/init" "$z/lib/fuzzer.zig"
libc="$z/lib/libc"
rm -rf "$libc/darwin" "$libc/freebsd" "$libc/netbsd" "$libc/wasi" "$libc/glibc" "$libc/musl"
for d in "$libc"/include/*; do
    case "${d##*/}" in
        any-windows-any) ;;
        *) rm -rf "$d" ;;
    esac
done
# clang headers for other CPU families (x86_64 never includes them) and the
# mingw-w64 32-bit and ARM parts
inc="$z/lib/include"
rm -f "$inc"/arm_*.h "$inc"/arm64_*.h "$inc"/altivec.h "$inc"/vecintrin.h "$inc"/msa.h \
    "$inc"/riscv_*.h "$inc"/sifive_*.h "$inc"/hvx_*.h "$inc"/hexagon_*.h "$inc"/lasxintrin.h \
    "$inc"/lsxintrin.h "$inc"/wasm_simd128.h "$inc"/spe.h "$inc"/htmintrin.h "$inc"/htmxlintrin.h \
    "$inc"/ppc_wrappers 2>/dev/null || true
rm -rf "$inc/ppc_wrappers" "$libc/mingw/lib32" "$libc/mingw/libarm32" "$libc/mingw/libarm64"
