#!/bin/sh

report=$($BIN -c 'koshkit --color never evilnet --failures' 2>/dev/null)
case $report in
  *'TCP'*'GROUP'*'Opens'*'Failures'*) failure_shape=matched ;;
  *) failure_shape=wrong ;;
esac
if [ "$(uname -s)" = Linux ]; then
  case $report in
    *'Connections'*) ;;
    *) failure_shape=wrong ;;
  esac
fi
printf 'failure-layout=%s\n' "$failure_shape"
case $report in
  *'NAME'*'RX'*'TX'*) scope_shape=extra ;;
  *) scope_shape=isolated ;;
esac
printf 'failure-scope=%s\n' "$scope_shape"

conflict_status=0
$BIN -c 'koshkit evilnet --failures --live' >/dev/null 2>&1 || conflict_status=$?
printf 'live-conflict-status=%s\n' "$conflict_status"
bare_cumulative_status=0
$BIN -c 'koshkit evilnet --traffic --cumulative' >/dev/null 2>&1 ||
  bare_cumulative_status=$?
printf 'bare-cumulative-status=%s\n' "$bare_cumulative_status"

$BIN -c 'koshkit evilnet --live=bad' >/dev/null 2>&1
printf 'invalid-live-status=%s\n' "$?"
$BIN -c 'koshkit evilnet --cumulative=bad' >/dev/null 2>&1
printf 'invalid-cumulative-status=%s\n' "$?"
