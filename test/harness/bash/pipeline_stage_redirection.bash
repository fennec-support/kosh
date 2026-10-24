#!/bin/bash
# A pipeline stage owns its own status and its own end of every pipe. The
# claims measured here are the pipefail selection, the target a repeated output
# redirection binds to, the signal a producer receives once its consumer has
# left, and the descriptor swap through a spare descriptor.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

echo "== pipefail selection =="
set -o pipefail
( exit 3 ) | ( exit 5 ) | ( exit 0 )
echo "rightmost=$?"
( exit 3 ) | ( exit 0 ) | ( exit 0 )
echo "leftmost-only=$?"
( exit 0 ) | ( exit 0 ) | ( exit 0 )
echo "all-zero=$?"
set +o pipefail

echo "== repeated output target =="
{ echo one > "$dir/first" > "$dir/second"; } | cat
echo "first=[$(< "$dir/first")]"
echo "second=[$(< "$dir/second")]"

echo "== producer after the consumer leaves =="
yes | { head -1 > /dev/null; }
echo "group-consumer=${PIPESTATUS[0]}"
yes | ( head -1 > /dev/null )
echo "subshell-consumer=${PIPESTATUS[0]}"
{ yes; } | head -1 > /dev/null
echo "group-producer=${PIPESTATUS[0]}"

echo "== descriptor swap in a stage =="
echo swapped-out 3>&1 >&3 | cat
{ echo swapped-err >&2; } 3>&1 1>&2 2>&3 | cat
