#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/background_terminal_pty.py"
printf 'status=%s\n' "$?"
