#!/bin/sh

set -m

probe_directory=$TEST_TEMP_DIRECTORY/live-sanitizers
mkdir -p "$probe_directory"

start_probe() {
  probe_name=$1
  probe_command=$2
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:symbolize=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=0 \
    "$BIN" -Q -c "$probe_command" \
    > "$probe_directory/$probe_name.out" \
    2> "$probe_directory/$probe_name.err" &
  echo $! > "$probe_directory/$probe_name.pid"
}

finish_probe() {
  probe_name=$1
  probe_pid=$(cat "$probe_directory/$probe_name.pid")
  kill -INT "$probe_pid" 2> "$TEST_NULL_DEVICE"
  attempt=0
  while [ "$attempt" -lt 50 ] && kill -0 "$probe_pid" 2> "$TEST_NULL_DEVICE"
  do
    attempt=$((attempt + 1))
    sleep 0.1
  done
  kill -KILL "$probe_pid" 2> "$TEST_NULL_DEVICE"
  wait "$probe_pid"
  probe_status=$?

  probe_errors=$(cat "$probe_directory/$probe_name.err")
  case $probe_errors in
    '') probe_diagnostics=none ;;
    *) probe_diagnostics=found ;;
  esac
  probe_output=$(cat "$probe_directory/$probe_name.out")
  case $probe_output in
    *'  LIVE  '*) probe_frames=printed ;;
    *) probe_frames=missing ;;
  esac
  printf '%s status=%s diagnostics=%s frames=%s\n' "$probe_name" \
    "$probe_status" "$probe_diagnostics" "$probe_frames"
  if [ "$probe_diagnostics" = found ]
  then
    printf '%s\n' "$probe_errors" >&2
  fi
}

start_probe evilio-process \
  'koshkit --color never evilio --ps --live=0.02 --cumulative=0.05 -5'
start_probe evilio-disk \
  'koshkit --color never evilio --live=0.02 --cumulative=0.05'
start_probe evilnet \
  'koshkit --color never evilnet --traffic --live=0.02 --cumulative=0.05'
start_probe evilps \
  'koshkit --color never evilps --cpu --live=0.02 --cumulative=0.05 -5'
start_probe evilss 'koshkit --color never evilss --live=0.02'

sleep 2

for probe_name in evilio-process evilio-disk evilnet evilps evilss
do
  finish_probe "$probe_name"
done

test -n "$probe_directory" && "$BIN_DIR/invoke-koshkit" rm -rf "$probe_directory"
