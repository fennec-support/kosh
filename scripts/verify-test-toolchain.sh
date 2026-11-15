#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Verifies the POSIX test tools shared by Linux and macOS CI.

set -eu

for tool in make clang++ python3 perl script expect mkfifo strings; do
    command -v "$tool"
done
test -x /bin/stty
"${BASHP:-bash}" --version
"${DASH:-dash}" -c 'echo dash available'
make --version
clang++ --version
python3 --version
