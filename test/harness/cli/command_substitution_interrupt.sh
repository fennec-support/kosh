#!/bin/bash

d=$(mktemp -d) || exit 1
trap 'test -n "$d" && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT

"$BIN" --mood bash -c \
  'value=$(echo ready > "$1"; (koshkit sleep 0.05; kill -INT "$$") & while :; do :; done)' \
  shell "$d/ready" > "$d/output" 2>&1
status=$?
test -s "$d/ready" || exit 1

echo "status=$status"
grep -A2 'error: Interrupted' "$d/output"
if grep -q 'Could not read command substitution output' "$d/output"; then
    echo pipe-read-error
else
    echo clean-interrupt
fi
