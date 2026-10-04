#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/koshkit_live_pty.py"
printf 'status=%s\n' "$?"
