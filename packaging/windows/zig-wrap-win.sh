#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# zig-wrap-win.sh ZIG TOOL [ARGS...]: like cmake/zig-wrap.sh for the
# windows-gnu target, and it also rewrites "-Xlinker X" to "-Wl,X" inside
# the @response files CMake writes for the link libraries (zig cc takes only
# the -Wl form, and --push-state/--pop-state not at all).
zig=$1
shift
x=
for a do
    shift
    case $a in
    @*)
        f=${a#@}
        if [ -f "$f" ]; then
            sed -i -e 's/-Xlinker /-Wl,/g' -e 's/-Wl,--push-state,//g' -e 's/-Wl,--pop-state//g' "$f"
        fi ;;
    esac
    if [ -n "$x" ]; then
        set -- "$@" "-Wl,$a"
        x=
    elif [ "$a" = -Xlinker ]; then
        x=1
    else
        set -- "$@" "$a"
    fi
done
exec "$zig" "$@"
