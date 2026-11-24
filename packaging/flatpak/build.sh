#!/bin/sh
# SPDX-FileCopyrightText: 2026 jerbanpls
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Build the Flatpak from the committed HEAD (docs/PACKAGING.md). From anywhere
# in the checkout (a git worktree works too):
#
#   packaging/flatpak/build.sh [FLATPAK-BUILDER ARGS...]
#
# Default args: --user --force-clean --repo=build-flatpak-repo build-flatpak.
# Extra args replace them, for example:
#   packaging/flatpak/build.sh --user --install --force-clean build-flatpak
#
# The engine's source is `git archive HEAD`, unpacked fresh into
# build-flatpak-src/ct-recomp (git-ignored) for the manifest's dir source.
# So only committed files go in: never rom/, a build tree, a module or
# uncommitted changes. A flatpak-builder `git` source can't do this: it
# mirrors the whole repository and checks out a named branch, not the
# worktree's HEAD. (A dir, not the tar itself: flatpak-builder caches a
# local archive source by its path alone, so a new commit would not
# rebuild.)
set -eu

top=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$top"

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
    echo "build.sh: uncommitted changes are NOT in the build (it uses HEAD $(git rev-parse --short HEAD))" >&2
fi
rm -rf build-flatpak-src
mkdir -p build-flatpak-src/ct-recomp
git archive --format=tar HEAD | tar -x -C build-flatpak-src/ct-recomp

if [ $# -eq 0 ]; then
    set -- --user --force-clean --repo=build-flatpak-repo build-flatpak
fi
if command -v flatpak-builder >/dev/null 2>&1; then
    exec flatpak-builder "$@" packaging/flatpak/io.github.jerbanpls.CtRecomp.yml
fi
exec flatpak run org.flatpak.Builder "$@" packaging/flatpak/io.github.jerbanpls.CtRecomp.yml
