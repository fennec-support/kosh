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
mkfifo "$dir/second" "$dir/third"

(
  attempt=0
  while [ -d "$dir" ] && [ "$attempt" -lt 300 ]; do
    /bin/sleep 0.1
    attempt=$((attempt + 1))
  done
  if [ -d "$dir" ]; then
    for fifo in first second third; do
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

echo open-that-no-signal-reaches
( /bin/sleep 1; echo quiet-payload > "$dir/third" ) &
writer=$!
read -r line < "$dir/third"
echo "quiet-status=$? quiet-line=$line"
wait "$writer" 2> /dev/null
echo open-that-no-signal-reaches-done

echo trap-during-blocking-open-done
