#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/editor_pending_tab_pty.py"
printf 'status=%s\n' "$?"
