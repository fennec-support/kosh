#!/bin/bash
# shellcheck disable=unresolved-command-uncertain

# Each compound form a coprocess takes, with and without a name. The body alone
# runs in the coprocess, so it answers once and exits with its own status.
echo "== unnamed brace group =="
coproc { read -r line; printf '%s\n' "brace:$line"; exit 4; }
brace_pid=$COPROC_PID
printf '%s\n' "elements:${#COPROC[@]} pid:${COPROC_PID:+set}"
printf 'one\n' >&"${COPROC[1]}"
read -r reply <&"${COPROC[0]}"
printf '%s\n' "reply:$reply"
wait "$brace_pid"
printf '%s\n' "wait:$?"

echo "== unnamed subshell =="
coproc ( read -r line; printf '%s\n' "subshell:$line"; exit 5 )
subshell_pid=$COPROC_PID
printf 'two\n' >&"${COPROC[1]}"
read -r reply <&"${COPROC[0]}"
printf '%s\n' "reply:$reply"
wait "$subshell_pid"
printf '%s\n' "wait:$?"

echo "== named brace group =="
coproc NAMED { read -r line; printf '%s\n' "named:$line"; }
named_pid=$NAMED_PID
printf '%s\n' "elements:${#NAMED[@]} pid:${NAMED_PID:+set}"
printf 'three\n' >&"${NAMED[1]}"
read -r reply <&"${NAMED[0]}"
printf '%s\n' "reply:$reply"
wait "$named_pid"
printf '%s\n' "wait:$?"

echo "== named subshell =="
coproc PAREN ( read -r line; printf '%s\n' "paren:$line" )
paren_pid=$PAREN_PID
printf 'four\n' >&"${PAREN[1]}"
read -r reply <&"${PAREN[0]}"
printf '%s\n' "reply:$reply"
wait "$paren_pid"
printf '%s\n' "wait:$?"

# A word before a simple command is that command, and the coprocess keeps the
# default name.
echo "== word before a simple command =="
WORDY() { read -r line; printf '%s\n' "wordy:$*:$line"; }
coproc WORDY a b
wordy_pid=$COPROC_PID
printf '%s\n' "named array:${WORDY+set} pid:${WORDY_PID+set}"
printf 'five\n' >&"${COPROC[1]}"
read -r reply <&"${COPROC[0]}"
printf '%s\n' "reply:$reply"
wait "$wordy_pid"
printf '%s\n' "wait:$?"

# A line inside a function that eval defined counts from the line of the eval
# command, wherever the function is called.
echo "== eval function lines =="
eval 'evalfn() {
  printf "%s\n" "line:$LINENO caller:${BASH_LINENO[*]} names:${FUNCNAME[*]}"
}'
evalfn
outer() {
  evalfn
}
outer
evalfn | cat
( evalfn ) &
wait "$!"

eval '

nested() {
  printf "%s\n" "nested:$LINENO"
}'
nested

definer() {
  eval 'inner() {
    printf "%s\n" "inner:$LINENO"
  }'
}
definer
inner
