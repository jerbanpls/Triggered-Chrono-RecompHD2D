# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross build for 64-bit Windows (MinGW ABI) with zig cc, from Linux
# (packaging/windows/build.sh):
#   cmake -B build-win64 -DCMAKE_TOOLCHAIN_FILE=cmake/zig-windows.cmake \
#         -DCT_ZIG=/path/to/zig -DCMAKE_PREFIX_PATH=<SDL2 mingw>/x86_64-w64-mingw32
# Tests are Windows programs: CMAKE_CROSSCOMPILING_EMULATOR is CT_WINE_RUN if
# set (packaging/windows/test-wine.sh does not use it; it runs them itself).
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT CT_ZIG)
    set(CT_ZIG $ENV{CT_ZIG})
endif()
if(NOT CT_ZIG)
    find_program(CT_ZIG zig REQUIRED)
endif()
set(CT_ZIG ${CT_ZIG} CACHE FILEPATH "zig used as the cross compiler")
set(CT_ZIG_TARGET x86_64-windows-gnu CACHE STRING "zig -target")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES CT_ZIG CT_ZIG_TARGET)

# One-word wrappers through packaging/windows/zig-wrap-win.sh (-Xlinker X ->
# -Wl,X; whole-archive as /WHOLEARCHIVE:).
set(ct_zig_wrap ${CMAKE_BINARY_DIR}/zig-wrap)
foreach(t cc c++ ar ranlib)
    set(ct_args ${t})
    if(t STREQUAL "cc" OR t STREQUAL "c++")
        set(ct_args "${t} -target ${CT_ZIG_TARGET}")
    endif()
    file(WRITE ${ct_zig_wrap}/zig-${t}
        "#!/bin/sh\nexec \"${CMAKE_CURRENT_LIST_DIR}/../packaging/windows/zig-wrap-win.sh\" \"${CT_ZIG}\" ${ct_args} \"$@\"\n")
    file(CHMOD ${ct_zig_wrap}/zig-${t} PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()
set(CMAKE_C_COMPILER ${ct_zig_wrap}/zig-cc)
set(CMAKE_CXX_COMPILER ${ct_zig_wrap}/zig-c++)
set(CMAKE_AR ${ct_zig_wrap}/zig-ar CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB ${ct_zig_wrap}/zig-ranlib CACHE FILEPATH "" FORCE)
set(CT_CCACHE CT_CCACHE-NOTFOUND CACHE FILEPATH "" FORCE)
set(CMAKE_LINKER_PUSHPOP_STATE_SUPPORTED FALSE)
set(CMAKE_C_LINKER_PUSHPOP_STATE_SUPPORTED FALSE)
set(CMAKE_CXX_LINKER_PUSHPOP_STATE_SUPPORTED FALSE)

if(CT_WINE_RUN)
    set(CMAKE_CROSSCOMPILING_EMULATOR ${CT_WINE_RUN})
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
