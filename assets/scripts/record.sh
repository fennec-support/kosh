#!/usr/bin/bash

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Records each named demo and renders it into assets/NAME-demo.gif with agg.
# Without operands, it records every demo. Run setup.sh first.
#
# Usage: record.sh [selector|lint|code|lsp ...]

set -eu

SCRIPTS=$(cd -- "$(dirname -- "$0")" && pwd)
ASSETS=$(dirname -- "$SCRIPTS")
DEMO_DIRECTORY=${KOSH_DEMO_DIRECTORY:-${TMPDIR:-/tmp}/kosh-demo}

if [ ! -x "$DEMO_DIRECTORY/env.sh" ]; then
  echo "record.sh: run setup.sh first." >&2
  exit 1
fi

if [ $# -eq 0 ]; then
  set -- selector lint code lsp
fi

# agg draws a no-break space as a euro sign, so the rendered copy of a cast
# carries plain spaces in its place.
NO_BREAK_SPACE=$(printf '\302\240')

for name in "$@"; do
  bash "$SCRIPTS/$name.sh"
  sed "s/$NO_BREAK_SPACE/ /g" "$DEMO_DIRECTORY/casts/$name.cast" \
    > "$DEMO_DIRECTORY/casts/$name-render.cast"
  "${AGG:-agg}" --theme kanagawa-dragon --font-family "JetBrains Mono" \
    --font-size 16 "$DEMO_DIRECTORY/casts/$name-render.cast" \
    "$ASSETS/$name-demo.gif" > /dev/null 2>&1
  echo "Recorded $ASSETS/$name-demo.gif."
done
