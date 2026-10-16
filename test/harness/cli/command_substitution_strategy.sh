#!/bin/sh

"$BIN" --mood bash -c '
fib()
{
    local n=$1 a b
    if ((n <= 1)); then
        echo "$n"
        return
    fi
    a=$(fib $((n - 1)))
    b=$(fib $((n - 2)))
    echo "$((a + b))"
}
value=$(fib 12)
external=$(/bin/echo external)
random=$(printf %s "$RANDOM")
name=BASHPID
indirect=$(printf %s "${!name}")
echo "$value $external"
case $random in ""|*[!0-9]*) echo random_bad;; *) echo random_digits;; esac
case $indirect in ""|*[!0-9]*) echo indirect_bad;; *) [ "$indirect" != "$BASHPID" ] && echo indirect_differs;; esac
'

"$BIN" --mood bash -c '
"$BIN" -c : &
parent=$!
ulimit -n 5
value=$(printf hi)
wait "$parent"
printf "snapshot-wait=%s value=<%s>\n" "$?" "$value"
' 2>&1
