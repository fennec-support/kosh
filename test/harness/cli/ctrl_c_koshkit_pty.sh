#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/ctrl_c_koshkit_pty.py"
printf 'status=%s\n' "$?"
