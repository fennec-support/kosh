#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/shell_integration_marks_pty.py"
printf 'status=%s\n' "$?"
