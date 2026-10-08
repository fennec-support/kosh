#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/prompt_hooks_pending_pty.py"
printf 'status=%s\n' "$?"
