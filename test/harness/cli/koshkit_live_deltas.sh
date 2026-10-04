#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/koshkit_live_deltas.py"
printf 'status=%s\n' "$?"
