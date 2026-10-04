#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/editor_ghost_menu_pty.py"
printf 'status=%s\n' "$?"
