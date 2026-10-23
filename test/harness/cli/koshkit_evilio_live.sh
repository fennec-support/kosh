#!/bin/sh

run_live_report() {
  live_report_path=$1
  live_command=$2
  : > "$live_report_path"
  "$BIN" -c \
    '(attempt=0; while [ "$attempt" -lt 250 ]; do
        if koshkit grep "  LIVE  " "$1" > /dev/null 2>&1; then break; fi
        koshkit sleep 0.02
        attempt=$((attempt + 1))
      done
      koshkit sleep 0.1
      kill -INT "$$") & '"$live_command" \
    live "$live_report_path" > "$live_report_path"
  live_status=$?
  live_report=$(cat "$live_report_path")
}

run_live_report "$TEST_TEMP_DIRECTORY/evilio-live-process-report" \
  'koshkit --color never evilio --live=0.05 --cumulative=0.02 --ps -1 --sort pid'
process_live_status=$live_status
process_live_report=$live_report
case $process_live_report in
  *evilio\ \ LIVE*window\ 0.02s,\ every\ 0.05s*PID*READ/0.02s*WRITE/0.02s*COMMAND*)
    live_shape=matched
    ;;
  *) live_shape=wrong ;;
esac
run_live_report "$TEST_TEMP_DIRECTORY/evilio-live-process-window-report" \
  'koshkit --color never evilio --live=0.05 --cumulative=1.2 --ps -1'
process_window_report=$live_report
case $process_window_report in
  *LIVE*"READ OPS/1.2s"*"WRITE OPS/1.2s"*)
    process_window_shape=matched
    ;;
  *) process_window_shape=wrong ;;
esac
run_live_report "$TEST_TEMP_DIRECTORY/evilio-live-suffix-report" \
  'koshkit --color never evilio --live=1s --cumulative=0.02 --ps -1'
suffix_live_status=$live_status
case $live_report in
  *evilio\ \ LIVE*every\ 1s*PID*COMMAND*) suffix_live_shape=matched ;;
  *) suffix_live_shape=wrong ;;
esac
case $process_live_report in
  *DEVICE*|*MEMORY*|*SWAP*) live_scope=extra ;;
  *) live_scope=only-process-io ;;
esac

run_live_report "$TEST_TEMP_DIRECTORY/evilio-live-disk-report" \
  'koshkit --color never evilio --live=0.05 --cumulative=0.02 --sort read'
disk_live_status=$live_status
disk_live_report=$live_report
case $disk_live_report in
  *LIVE*DEVICE*READ/0.02s*WRITE/0.02s*READ\ OPS/0.02s*WRITE\ OPS/0.02s*BUSY*READ\ LAT*WRITE\ LAT*AVG\ QUEUE*QUEUE*ERRORS*RETRIES*)
    disk_live_shape=matched
    ;;
  *) disk_live_shape=wrong ;;
esac
case $disk_live_report in
  *DISKS*|*MEMORY*|*SWAP*) disk_live_scope=extra ;;
  *) disk_live_scope=only-disk-io ;;
esac
case $disk_live_report in
  evilio\ \ LIVE*) disk_live_margin=unindented ;;
  *) disk_live_margin=wrong ;;
esac
run_live_report "$TEST_TEMP_DIRECTORY/evilio-live-disk-window-report" \
  'koshkit --color never evilio --live=0.05 --cumulative=1.2'
disk_window_report=$live_report
case $disk_window_report in
  *LIVE*"READ OPS/1.2s"*"WRITE OPS/1.2s"*)
    disk_window_shape=matched
    ;;
  *) disk_window_shape=wrong ;;
esac

printf 'status=%s\n' "$process_live_status"
printf 'layout=%s\n' "$live_shape"
printf 'process-window-layout=%s\n' "$process_window_shape"
printf 'suffix-status=%s\n' "$suffix_live_status"
printf 'suffix-layout=%s\n' "$suffix_live_shape"
printf 'scope=%s\n' "$live_scope"
printf 'disk-status=%s\n' "$disk_live_status"
printf 'disk-layout=%s\n' "$disk_live_shape"
printf 'disk-scope=%s\n' "$disk_live_scope"
printf 'disk-margin=%s\n' "$disk_live_margin"
printf 'disk-window-layout=%s\n' "$disk_window_shape"

$BIN -c 'koshkit evilio --live=bad' > /dev/null 2>&1
printf 'rejected-live-status=%s\n' "$?"
