#!/bin/bash
# Bash dynamic variables $RANDOM, $SECONDS, $EPOCHSECONDS, $BASHPID. The values
# are non-deterministic, so the script asserts their properties, which match bash
# byte-for-byte. OSTYPE is nonempty. HISTCMD ignores assignment and OPTIND keeps
# its value across getopts, $_ holds the last argument of the previous simple
# command, and EPOCHREALTIME has seconds and a six digit fraction.
initial_seconds=$SECONDS
[ "$initial_seconds" -ge 0 ] && [ "$initial_seconds" -le 1 ] && echo "seconds starts zero"
r=$RANDOM
[ "$r" -ge 0 ] && [ "$r" -le 32767 ] && echo "random in range"
a=$RANDOM
b=$RANDOM
c=$RANDOM
[ "$a$b$c" != "000" ] && echo "random produces values"
[ "$BASHPID" -gt 0 ] && echo "bashpid positive"
[ "$EPOCHSECONDS" -gt 1000000000 ] && echo "epoch is a unix time"
[ "$UID" -ge 0 ] && echo "uid present"
[ "$EUID" -ge 0 ] && echo "euid present"
[ "$PPID" -gt 0 ] && echo "ppid positive"
[ "$BASH_MONOSECONDS" -gt 0 ] && echo "monoseconds positive"
echo "hosttype=$HOSTTYPE"
case $MACHTYPE in
"$HOSTTYPE"-*-"$OSTYPE"*) echo "machtype holds the host and system" ;;
*) echo "machtype=$MACHTYPE" ;;
esac
[ "$GROUPS" -ge 0 ] && echo "groups present"
[ -n "$SRANDOM" ] && echo "srandom present"
for name in EUID UID PPID BASHPID RANDOM SECONDS SRANDOM LINENO; do
  declaration=$(declare -p "$name")
  echo "${declaration%%=*}"
done
[ -n "$OSTYPE" ] && echo "ostype present"

echo "optind=[$OPTIND]"
echo "histcmd=[$HISTCMD]"
declare -p OPTIND
declare -p HISTCMD
HISTCMD=5
echo "after-histcmd=[$HISTCMD]"
declare -p HISTCMD
OPTIND=3
echo "after-optind=[$OPTIND]"
declare -p OPTIND
set -- -a -b operand
OPTIND=1
while getopts ab option; do
  echo "option=$option optind=$OPTIND"
done
echo "final-optind=[$OPTIND]"
shift $((OPTIND - 1))
echo "remaining=[$*]"

# $_ expands to the last argument of the previous simple command, checked
# byte-for-byte against bash.
true alpha beta
echo "after_true=[$_]"
echo one two three
echo "after_echo=[$_]"
:
echo "after_colon=[$_]"
printf '%s\n' x y z
echo "after_printf=[$_]"

underscore_function() {
  printf 'function-entry=[%s]\n' "$_"
  : function-body
}
: before-function
underscore_function call-tail
printf 'function-after=[%s]\n' "$_"

: snapshot-parent
printf 'snapshot-sub=[%s] snapshot-underscore=[%s]\n' \
  "$(: snapshot-child; printf child-output)" "$_"

r=$EPOCHREALTIME
[[ $r == *.* ]] && echo "has-dot"
frac=${r#*.}
echo "frac-len: ${#frac}"
sec=${r%.*}
case $sec in
  [0-9]*) echo "sec-numeric" ;;
  *) echo "sec-bad" ;;
esac
echo "base: $((10#0${r%.*} > 0))"
