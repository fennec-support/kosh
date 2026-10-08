#!/bin/sh

BIN="$BIN" python3 "$(dirname "$0")/startup_chain_pty.py"
printf 'status=%s\n' "$?"
