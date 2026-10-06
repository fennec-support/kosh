#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/editor_kill_yank_pty.py"
printf 'status=%s\n' "$?"
