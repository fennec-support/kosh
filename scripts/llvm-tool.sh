#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

tool=$1
shift

if [ "$(uname -s)" = Darwin ]; then
    exec xcrun "$tool" "$@"
fi

exec "$tool" "$@"
