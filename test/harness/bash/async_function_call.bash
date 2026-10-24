#!/bin/bash

# A function called with a trailing ampersand runs in a background subshell.
# The call returns at once, the shell records its process in the last
# background variable, and wait reports the status the function returned or
# exited with. An exit inside the function ends only the subshell, and the
# variables the function assigns never reach the caller. A gate FIFO orders
# the background output after the foreground output without timing. The words
# of the call expand exactly once: a command substitution in an argument
# appends one line to a counter file each time it runs.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

if ! mkfifo "$dir/gate" 2> /dev/null; then
  echo skip-no-fifo
  exit 0
fi

echo ordering
gated() {
  read -r _ < "$dir/gate"
  echo "background-done $1"
}
gated one &
echo "after-call"
echo go > "$dir/gate"
wait
echo "ordering-done"

echo last-background-pid
quiet() { :; }
quiet &
if [ -n "$!" ]; then
  echo "pid-set"
fi
wait "$!"
echo "quiet-status=$?"

echo return-status
five() { return 5; }
five &
wait "$!"
echo "return-status=$?"

echo exit-status
leave() { exit 6; }
leave &
wait "$!"
echo "exit-status=$?"
echo "parent-survived"

echo exit-after-output
farewell() {
  echo "farewell-body"
  exit 7
  echo "farewell-unreachable"
}
farewell &
wait "$!"
echo "farewell-status=$?"

echo jobs-and-bare-wait
mkfifo "$dir/hold"
holder() {
  read -r _ < "$dir/hold"
  return 3
}
holder &
echo go > "$dir/hold"
wait
echo "bare-wait-status=$?"

echo no-leak
var=parent
declare -a arr=(parent)
mutate() {
  var=child
  arr=(child)
  newvar=child
  cd /
}
here=$PWD
mutate &
wait "$!"
echo "var=$var arr=${arr[*]} newvar=${newvar-unset}"
if [ "$PWD" = "$here" ]; then
  echo "cwd-kept"
fi

echo arguments-and-redirection
report() {
  echo "args=$# first=$1 second=$2"
}
report "a b" c > "$dir/out" &
wait "$!"
cat "$dir/out"

echo expansion-once
show() {
  echo "args=$# first=$1 second=$2"
}

echo substitution-argument
show "$(echo hit >> "$dir/count"; echo value)" tail &
wait "$!"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo substitution-with-redirection
: > "$dir/count"
show "$(echo hit >> "$dir/count"; echo value)" tail > "$dir/out" &
wait "$!"
cat "$dir/out"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo prefix-assignment
: > "$dir/count"
mark() {
  echo "mark=$MARK first=$1"
}
MARK=set mark "$(echo hit >> "$dir/count"; echo value)" &
wait "$!"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo expanded-function-name
: > "$dir/count"
fn=show
$fn "$(echo hit >> "$dir/count"; echo value)" tail &
wait "$!"
echo "count=$(wc -l < "$dir/count" | tr -d ' ')"

echo async-function-done
