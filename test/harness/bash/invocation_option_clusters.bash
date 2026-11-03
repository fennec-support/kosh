#!/bin/bash

# Each o or O in a cluster of invocation options takes the next unused
# argument as its option name, in the order of the letters, and the letters
# after it remain options. A c in the same cluster takes the first operand
# after those names as the command string.
show='for name in errexit noglob noclobber; do
  shopt -qo "$name" && printf "%s " "$name"
done
for name in dotglob nocasematch; do shopt -q "$name" && printf "%s " "$name"; done
printf "0=%s\n" "$0"'

"$BASH" -oc errexit "$show" zero
"$BASH" -Oc dotglob "$show" zero
"$BASH" -co errexit "$show" zero
"$BASH" -cO dotglob "$show" zero
"$BASH" -ooc errexit noglob "$show" zero
"$BASH" -oOc errexit nocasematch "$show" zero
"$BASH" -fo errexit -c "$show" zero
"$BASH" -Co noglob -c "$show" zero
"$BASH" -oO errexit dotglob -c "$show" zero
"$BASH" -o errexit +o errexit -c "$show" zero
"$BASH" -O dotglob +O dotglob -c "$show" zero
"$BASH" -f +Oo dotglob noglob -c "$show" zero

# The name is never taken from the rest of the cluster, so the c after o is
# not a name, and a word that follows a lone o is its name even when it
# begins with a dash.
"$BASH" -oc bogus "$show" zero 2>/dev/null
echo "unknown=$?"
"$BASH" -o -c "$show" zero 2>/dev/null
echo "dash=$?"

# A last -o, +o, -O, or +O without a name lists the options the way set and
# shopt do, and the shell then takes commands from standard input. After a
# command string, -o is $0.
echo 'echo ran' | "$BASH" -o errexit -o | grep -E '^(errexit|noglob) |^ran'
echo 'echo ran' | "$BASH" -f +o | grep -E '^set [-+]o (errexit|noglob)$|^ran'
echo 'echo ran' | "$BASH" -O | grep -E '^(dotglob|nocasematch) |^ran'
echo 'echo ran' | "$BASH" +O | grep -E '^shopt -[su] (dotglob|nocasematch)$|^ran'
"$BASH" -c 'echo "zero=$0"' -o
