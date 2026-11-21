#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Run a command under Wine in a throwaway podman container (EXPERIMENTAL
# Windows package tests).
#
#   packaging/windows/wine.sh OUT -- CMD...
#
# OUT/stage is visible as C:\pkg, which is also the working directory; CMD
# is an ordinary command line of the container (so "wine prog.exe args").
# The image `ct-wine` (Containerfile: Fedora, wine, a non-root user, a
# pre-initialised 64-bit prefix) is built on first use; after that no
# network is needed. Extra podman options: CT_WINE_PODMAN_ARGS (for example
# "-v /tmp/x:/data" to see a host directory inside).
# SDL runs on its dummy video and audio drivers (no display, no sound).
set -eu

here=$(cd "$(dirname "$0")" && pwd)
[ $# -ge 2 ] && [ "$2" = -- ] || { echo "usage: wine.sh OUT -- CMD..." >&2; exit 2; }
out=$(cd "$1" && pwd)
shift 2
[ -d "$out/stage" ] || { echo "wine.sh: $out/stage missing (run build.sh first)" >&2; exit 1; }

if ! podman image exists ct-wine; then
    podman build -t ct-wine -f "$here/Containerfile" "$here" >&2
fi
# shellcheck disable=SC2086
exec podman run --rm --network=none --security-opt label=disable --userns=keep-id:uid=1000,gid=1000 \
    -v "$out/stage:/home/wine/prefix/drive_c/pkg" -w /home/wine/prefix/drive_c/pkg \
    -e SDL_VIDEODRIVER=dummy -e SDL_AUDIODRIVER=dummy ${CT_WINE_PODMAN_ARGS:-} \
    ct-wine "$@"
