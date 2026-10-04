#!/bin/bash

# An open of a named pipe blocks until a writer arrives. A trapped signal that
# lands while the open blocks runs its action and the open resumes, so the
# redirection still reaches the file and the read succeeds. A trapped interrupt
# behaves the same way. The whole file is skipped where named pipes are
# unavailable, and both shells take that branch together.
#
# The writer of a signalled case waits for the notifier to leave a marker after
# it sent the signal, so the signal can never lose a race against the writer
# however slowly the machine runs. The notifier waits until the reader is blocked in the open
# where the kernel reports it, and otherwise falls back to a sleep. A watchdog
# feeds every pipe after a bounded time, so a lost signal or writer fails the
# comparison with a visible marker line instead of hanging the run.
#
# The action case reverses the dependency. Its writer waits for a marker that
# only the trap action creates, so the action must run while the open is still
# blocked and the open must resume afterwards.

dir=$(mktemp -d)
if [ ! -d "$dir" ]; then
  echo could-not-make-a-directory
  exit 1
fi
trap 'rm -rf "$dir"' EXIT

if ! mkfifo "$dir/first" 2> /dev/null; then
  echo named-pipes-are-unavailable
  echo trap-during-blocking-open-done
  exit 0
fi
mkfifo "$dir/second" "$dir/third" "$dir/fourth" "$dir/fifth" "$dir/sixth"
mkfifo "$dir/seventh" "$dir/eighth" "$dir/ninth"

(
  attempt=0
  while [ -d "$dir" ] && [ "$attempt" -lt 300 ]; do
    /bin/sleep 0.1
    attempt=$((attempt + 1))
  done
  if [ -d "$dir" ]; then
    for fifo in first second third fourth fifth sixth seventh eighth ninth; do
      echo watchdog-fired > "$dir/$fifo" 2> /dev/null &
    done
  fi
) &

wait_until_blocked() {
  local attempt=0
  if [ ! -r "/proc/$1/wchan" ]; then
    /bin/sleep 1
    return 0
  fi
  while [ "$attempt" -lt 60 ]; do
    case "$(cat "/proc/$1/wchan" 2> /dev/null)" in
      *partner* | *fifo*) return 0 ;;
    esac
    /bin/sleep 0.05
    attempt=$((attempt + 1))
  done
}

wait_for_marker() {
  local attempt=0
  while [ ! -e "$1" ] && [ "$attempt" -lt 200 ]; do
    /bin/sleep 0.05
    attempt=$((attempt + 1))
  done
}

echo open-under-a-trapped-signal
trap 'echo action-signal' USR1
( wait_until_blocked $$; kill -USR1 $$; : > "$dir/signal-sent" ) &
notifier=$!
( wait_for_marker "$dir/signal-sent"; echo signal-payload > "$dir/first" ) &
writer=$!
read -r line < "$dir/first"
echo "signal-status=$? signal-line=$line"
wait "$notifier" 2> /dev/null
wait "$writer" 2> /dev/null
trap - USR1
echo open-under-a-trapped-signal-done

echo open-under-a-trapped-interrupt
trap 'echo action-interrupt' INT
( wait_until_blocked $$; kill -INT $$; : > "$dir/interrupt-sent" ) &
notifier=$!
( wait_for_marker "$dir/interrupt-sent"; echo interrupt-payload > "$dir/second" ) &
writer=$!
read -r line < "$dir/second"
echo "interrupt-status=$? interrupt-line=$line"
wait "$notifier" 2> /dev/null
wait "$writer" 2> /dev/null
trap - INT
echo open-under-a-trapped-interrupt-done

echo open-whose-writer-waits-for-the-action
trap 'echo action-marker; : > "$dir/action-ran"' USR2
( wait_until_blocked $$; kill -USR2 $$ ) &
notifier=$!
(
  wait_for_marker "$dir/action-ran"
  if [ -e "$dir/action-ran" ]; then
    echo action-payload > "$dir/fourth"
  else
    echo action-never-ran > "$dir/fourth"
  fi
) &
writer=$!
read -r line < "$dir/fourth"
echo "action-status=$? action-line=$line"
wait "$notifier" 2> /dev/null
wait "$writer" 2> /dev/null
trap - USR2
echo open-whose-writer-waits-for-the-action-done

echo open-whose-action-exits
(
  me=$BASHPID
  trap 'echo exit-trap-ran' EXIT
  trap 'echo action-exit; exit 7' USR1
  ( wait_until_blocked "$me"; kill -USR1 "$me" ) &
  read -r line < "$dir/fifth"
  echo "exit-case-survived status=$? line=$line"
)
echo "exit-case-status=$?"
echo open-whose-action-exits-done

echo open-whose-action-returns
trap 'echo action-return; return 3' USR1
return_from_function() {
  read -r line < "$dir/sixth"
  echo "return-case-survived status=$?"
  return 9
}
( wait_until_blocked $$; kill -USR1 $$ ) &
notifier=$!
return_from_function
echo "return-case-status=$?"
wait "$notifier" 2> /dev/null
trap - USR1
echo open-whose-action-returns-done

echo open-whose-action-returns-from-a-group
trap 'echo action-group-return; return 4' USR1
return_from_group() {
  { cat; } < "$dir/seventh"
  echo "group-case-survived status=$?"
  return 9
}
( wait_until_blocked $$; kill -USR1 $$ ) &
notifier=$!
return_from_group
echo "group-case-status=$?"
wait "$notifier" 2> /dev/null
trap - USR1
echo open-whose-action-returns-from-a-group-done

echo open-whose-action-sets-a-variable
trap 'marker=action-ran' USR1
marker=untouched
( wait_until_blocked $$; kill -USR1 $$; : > "$dir/variable-sent" ) &
notifier=$!
( wait_for_marker "$dir/variable-sent"; echo variable-payload > "$dir/ninth" ) &
writer=$!
read -r line < "$dir/ninth"
echo "variable-status=$? variable-line=$line marker=$marker"
wait "$notifier" 2> /dev/null
wait "$writer" 2> /dev/null
trap - USR1
echo open-whose-action-sets-a-variable-done

echo open-whose-action-breaks
trap 'echo action-break; break' USR1
for round in 1 2 3; do
  echo "round=$round"
  ( wait_until_blocked $$; kill -USR1 $$; : > "$dir/break-sent" ) &
  notifier=$!
  ( wait_for_marker "$dir/break-sent"; echo break-payload > "$dir/eighth" ) &
  writer=$!
  read -r line < "$dir/eighth"
  echo "break-case-survived status=$? line=$line"
done
echo "break-case-status=$?"
wait "$notifier" 2> /dev/null
wait "$writer" 2> /dev/null
trap - USR1
echo open-whose-action-breaks-done

echo open-that-no-signal-reaches
( /bin/sleep 1; echo quiet-payload > "$dir/third" ) &
writer=$!
read -r line < "$dir/third"
echo "quiet-status=$? quiet-line=$line"
wait "$writer" 2> /dev/null
echo open-that-no-signal-reaches-done

echo trap-during-blocking-open-done
