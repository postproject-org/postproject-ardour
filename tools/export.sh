#!/bin/sh
# SPDX-FileCopyrightText: 2026 Erich Seifert <dev@erichseifert.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Regenerates patches/ from the commits on top of the pinned revision.
# Usage: tools/export.sh [tree]   (default: ./ardour)
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/UPSTREAM"
tree=${1:-"$here/ardour"}

rm -f "$here"/patches/*.patch
git -C "$tree" format-patch -q --zero-commit --no-signature --no-stat --abbrev=9 \
    -o "$here/patches" "$ARDOUR_COMMIT..HEAD"
(cd "$here/patches" && ls ./*.patch | sed 's|^\./||') > "$here/patches/series"
cat "$here/patches/series"

