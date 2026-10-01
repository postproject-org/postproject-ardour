#!/bin/sh
# SPDX-FileCopyrightText: 2026 Erich Seifert <dev@erichseifert.de>
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Checks out the pinned Ardour revision and applies the pilot patch series.
# Usage: tools/checkout.sh [tree]   (default: ./ardour)
set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
. "$here/UPSTREAM"
tree=${1:-"$here/ardour"}

if [ ! -d "$tree/.git" ]; then
    git init -q "$tree"
elif [ -n "$(git -C "$tree" status --porcelain)" ]; then
    echo "error: $tree has uncommitted changes" >&2
    exit 1
fi

git -C "$tree" fetch -q --depth 1 "$ARDOUR_URL" \
    "refs/tags/$ARDOUR_BASE_TAG:refs/tags/$ARDOUR_BASE_TAG"
git -C "$tree" fetch -q --depth "$ARDOUR_HISTORY_DEPTH" \
    "$ARDOUR_URL" "$ARDOUR_COMMIT"
fetched=$(git -C "$tree" rev-parse FETCH_HEAD)
if [ "$fetched" != "$ARDOUR_COMMIT" ]; then
    echo "error: fetched $fetched, expected $ARDOUR_COMMIT" >&2
    exit 1
fi
if ! git -C "$tree" describe --tags --match "$ARDOUR_BASE_TAG" \
    "$ARDOUR_COMMIT" >/dev/null 2>&1; then
    echo "error: history depth does not reach Ardour tag $ARDOUR_BASE_TAG" >&2
    exit 1
fi

git -C "$tree" checkout -q -B postproject-pilot "$ARDOUR_COMMIT"
while read -r patch; do
    case $patch in '' | '#'*) continue ;; esac
    GIT_COMMITTER_NAME=${GIT_COMMITTER_NAME:-PostProject pilot} \
    GIT_COMMITTER_EMAIL=${GIT_COMMITTER_EMAIL:-pilot@postproject.invalid} \
        git -C "$tree" am -q "$here/patches/$patch"
done < "$here/patches/series"
echo "$tree: postproject-pilot = $ARDOUR_COMMIT + $(git -C "$tree" rev-list --count "$ARDOUR_COMMIT..HEAD") patches"
