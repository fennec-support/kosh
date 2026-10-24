#!/bin/bash
# Bash getopts in silent mode, checked byte-for-byte against bash. The leading
# colon selects silent error handling, so a bad option and a missing argument
# arrive through the question-mark and colon cases with OPTARG set, while OPTIND
# advances across grouped and separate options.
parse() {
  local opt OPTIND=1
  while getopts ":ab:c" opt; do
    case $opt in
      a) echo "flag a";;
      b) echo "b arg=$OPTARG";;
      c) echo "flag c";;
      \?) echo "bad opt: $OPTARG";;
      :) echo "missing arg: $OPTARG";;
    esac
  done
  echo "OPTIND=$OPTIND"
}
parse -a -b val -c
parse -ab val
parse -x
parse -b
parse -a foo

# return outside a function and outside a sourced file is rejected and the shell
# continues, while return inside a function ends it with the status. The dot
# command passes its trailing operands to the sourced file as positional
# parameters and restores the caller's parameters afterward.
return 5
echo "after=$?"
f() { return 7; }
f
echo "fn=$?"
file=$(mktemp)
printf 'echo "in=$1 $2"\n' > "$file"
set -- keep1 keep2
. "$file" arg1 arg2
echo "out=$1 $2"
rm -f "$file"

read -rr repeated_raw <<< 'a\b'
printf 'repeated-raw=%s\n' "$repeated_raw"
read -r -Z 2>/dev/null || :
read parser_state <<< 'a\b'
printf 'parser-state=%s\n' "$parser_state"
mapfile -Z 2>/dev/null; echo "mapfile-unknown=$?"
shopt -Z 2>/dev/null; echo "shopt-unknown=$?"
complete -Z 2>/dev/null; echo "complete-unknown=$?"
compgen -Z 2>/dev/null; echo "compgen-unknown=$?"
mapfile mapfile_first mapfile_ignored <<< x
echo "mapfile-extra=${#mapfile_first[@]}"
compgen -W 'alpha beta' alpha ignored
echo "compgen-extra=$?"

# An OPTIND below 1 starts again at the first operand.
OPTIND=0
getopts a opt -a
echo "zero-optind=$? opt=$opt OPTIND=$OPTIND"
