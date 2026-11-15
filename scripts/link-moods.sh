#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Installs relative mood links without replacing unrelated files.

set -eu
cd "$1"
executable=$2
shift 2
for mood do
    if [ -e "$mood" ] || [ -L "$mood" ]; then
        if [ ! -L "$mood" ] || [ "$(readlink "$mood")" != "$executable" ]; then
            printf '%s is not a link to %s\n' "$mood" "$executable" >&2
            exit 1
        fi
    else
        ln -s "$executable" "$mood"
    fi
done
