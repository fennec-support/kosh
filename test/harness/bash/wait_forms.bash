#!/bin/bash

# The wait builtin keeps the status of a job after it is waited, so a second
# wait on its process reports that status again, while a bare wait forgets
# every status. An unknown process or job reports 127, and with several
# operands the last one decides the status. The -n option returns the next job
# to finish, -p stores the process whose status is returned, and -f waits for
# termination. A trapped signal ends a blocking wait -n with 128 plus its
# number. A finished process substitution among the operands of wait -n
# returns before a running job, and -p with operands replaces a name
# reference with a plain variable. Gate FIFOs order the jobs without timing.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

if ! mkfifo "$dir/gate" "$dir/second" 2> /dev/null; then
  echo skip-no-fifo
  exit 0
fi

echo repeated-wait
(exit 3) &
first=$!
wait "$first"
echo "first=$?"
wait "$first"
echo "again=$?"

echo unknown-targets
wait 99999
echo "unknown=$?"
wait %9
echo "unknown-job=$?"
wait abc
echo "not-a-pid=$?"

echo mixed-operands
(exit 4) &
known=$!
wait 99999 "$known"
echo "known-last=$?"
wait "$known" 99999
echo "unknown-last=$?"

echo job-specs
(exit 5) &
wait %1
echo "job=$?"
(exit 6) &
(exit 7) &
wait %2 %1
echo "two-jobs=$?"

echo next-job
(read -r _ < "$dir/gate"; exit 2) &
slow=$!
(exit 9) &
fast=$!
wait -n
echo "next=$?"
echo go > "$dir/gate"
wait -n
echo "following=$?"
wait -n
echo "none-left=$?"
wait "$fast"
echo "fast-again=$?"
wait -n "$slow"
echo "next-remembered=$?"

echo next-of-named
(read -r _ < "$dir/gate"; exit 11) &
held=$!
(exit 12) &
free=$!
(read -r _ < "$dir/second"; exit 13) &
other=$!
wait -n -p who "$held" "$free"
echo "named=$? who-is-free=$([ "$who" = "$free" ] && echo yes)"
echo go > "$dir/gate"
wait -n -p who "$held" 99999
echo "named-with-unknown=$? who-is-held=$([ "$who" = "$held" ] && echo yes)"
echo go > "$dir/second"
wait -n -p who %3
echo "named-job=$? who-is-other=$([ "$who" = "$other" ] && echo yes)"
who=old
wait -n 99999
echo "next-unknown=$?"
wait -n -p who
echo "next-none=$? who=${who-unset}"

echo pid-variable
(exit 8) &
eight=$!
wait -p who "$eight"
echo "stored=$? who-is-eight=$([ "$who" = "$eight" ] && echo yes)"
wait -p who 99999
echo "unknown-stored=$? who=${who-unset}"
who=old
(exit 1) &
wait -p who
echo "bare-stored=$? who=${who-unset}"
wait -p 1x "$eight"
echo "invalid-name=$?"

echo force
(exit 10) &
ten=$!
wait -f "$ten"
echo "forced=$?"
wait
wait -fn
echo "forced-next=$?"

echo bare-wait-forgets
(exit 3) &
forgotten=$!
wait
echo "bare=$?"
wait "$forgotten"
echo "forgotten=$?"

echo trapped-next
trap 'echo action-next' USR1
( /bin/sleep 1; kill -USR1 $$ ) &
notifier=$!
/bin/sleep 5 &
sleeper=$!
wait -n "$sleeper"
echo "interrupted=$?"
kill "$sleeper"
wait "$sleeper"
echo "killed=$?"
wait "$notifier"
trap - USR1

echo substitution-before-job
mkfifo "$dir/held"
exec {held_fd}< <(exit 6)
held_substitution=$!
{ read -r _ < "$dir/held"; exit 7; } &
held_job=$!
wait -n -p held_who "$held_job" "$held_substitution"
echo "next=$? substitution=$([ "$held_who" = "$held_substitution" ] && echo yes)"
echo release > "$dir/held"
wait "$held_job"
echo "job=$?"
exec {held_fd}<&-

echo pid-reference
declare -n pid_ref=pid_target
(exit 5) &
wait -n -p pid_ref "$!"
echo "status=$? target=${pid_target-unset} value=${pid_ref:+set}"
[[ -R pid_ref ]] || echo "pid_ref is no reference"

echo wait-forms-done
