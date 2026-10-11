#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/koshkit_meow_pty.py"
printf 'status=%s\n' "$?"
