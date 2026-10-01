#!/bin/sh
# SPDX-FileCopyrightText: 2026 Erich Seifert <dev@erichseifert.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Compiles and runs the resolver scenario through the installed pkg-config file.
# Usage: tools/test.sh INSTALL_PREFIX [build-directory]
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 INSTALL_PREFIX [build-directory]" >&2
    exit 2
fi

here=$(cd "$(dirname "$0")/.." && pwd)
prefix=$(cd "$1" && pwd)
build=${2:-"$here/build"}
mkdir -p "$build"

export PKG_CONFIG_PATH="$prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
pkg-config --print-errors --exists postproject

cxx=${CXX:-c++}
# shellcheck disable=SC2046
"$cxx" -std=c++17 -Wall -Wextra -Werror \
    -I"$here/ardour/gtk2_ardour" \
    "$here/tests/resolver_scenario.cc" \
    "$here/ardour/gtk2_ardour/postproject_resolver.cc" \
    -Wl,-rpath,"$prefix/lib" \
    $(pkg-config --cflags --libs postproject) \
    -o "$build/postproject-resolver-scenario"

test_root=$(mktemp -d "${TMPDIR:-/tmp}/postproject-ardour.XXXXXX")
trap 'rm -rf "$test_root"' EXIT HUP INT TERM
"$build/postproject-resolver-scenario" "$test_root"

