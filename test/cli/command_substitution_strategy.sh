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
pid=$(printf %s "$BASHPID")
ppid=$(printf %s "$PPID")
random=$(printf %s "$RANDOM")
srandom=$(printf %s "$SRANDOM")
name=BASHPID
indirect=$(printf %s "${!name}")
echo "$value $external"
'

"$BIN" --mood bash -c '
"$BIN" -c : &
parent=$!
ulimit -n 5
value=$(printf hi)
wait "$parent"
printf "snapshot-wait=%s value=<%s>\n" "$?" "$value"
' 2>&1
