#!/bin/sh
# SPDX-FileCopyrightText: 2026 Erich Seifert <dev@erichseifert.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Configures and builds the patched Ardour tree.
# Usage: tools/build.sh INSTALL_PREFIX|none
set -eu

if [ "$#" -ne 1 ]; then
    echo "usage: $0 INSTALL_PREFIX|none" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")/.." && pwd)
case $1 in
none)
    postproject_option=--no-postproject
    ;;
*)
    prefix=$(cd "$1" && pwd)
    export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    pkg-config --print-errors --exists postproject
    postproject_option=
    ;;
esac

jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
"$here/ardour/waf" configure \
    --prefix="$here/install" \
    --with-backends=jack \
    --no-phone-home \
    $postproject_option
"$here/ardour/waf" build -j"$jobs"

