#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/prompt_function_substitution_pty.py"
printf 'status=%s\n' "$?"
