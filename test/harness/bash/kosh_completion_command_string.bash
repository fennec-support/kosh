#!/bin/bash
# The bash completion for kosh offers options between -c and its command
# string, nothing for the command string itself, only -c and --command for the
# word after it, and files for later arguments and script operands.
d=$(mktemp -d)
cd "$d" || exit 1
: > alpha.sh
. "${BIN%/*}/completions/kosh.bash"

do_complete() {
  COMP_WORDS=("$@")
  COMP_CWORD=$(($# - 1))
  _kosh_complete
  printf '%s:' "$*"
  printf ' [%s]' "${COMPREPLY[@]}"
  printf '\n'
}

do_complete kosh -c ''
do_complete kosh -c -e ''
do_complete kosh -ec ''
do_complete kosh --command ''
do_complete kosh -c --no-e
do_complete kosh -c -o err
do_complete kosh -oc errexit ''
do_complete kosh -c 'echo hi' ''
do_complete kosh -c 'echo hi' --com
do_complete kosh -c 'echo hi' -c ''
do_complete kosh -c 'echo hi' zero al
do_complete kosh --command='echo hi' --com
do_complete kosh -c -- ''
do_complete kosh -e al
do_complete kosh alpha.sh --no-e
do_complete kosh -- al
do_complete kosh -M ''

cd / && rm -rf "$d"
