#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/editor_prompt_pty.py"
printf 'status=%s\n' "$?"
