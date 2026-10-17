#!/bin/sh

report=$($BIN -c 'koshkit --color never evilfiles --pid $$' 2> /dev/null)
case $report in
  *MODE*OFFSET*ENDPOINT*NAME*) schema=present ;;
  *) schema=missing ;;
esac
printf 'schema=%s\n' "$schema"

invalid_status=0
$BIN -c 'koshkit evilfiles --pid invalid' > /dev/null 2>&1 || invalid_status=$?
printf 'invalid-pid-status=%s\n' "$invalid_status"

case $report in
  *'(deleted)'*|*'[inaccessible]'*|*socket:*|*pipe*)
    descriptor_markers=present
    ;;
  *) descriptor_markers=capability-dependent
    ;;
esac
printf 'descriptor-markers=%s\n' "$descriptor_markers"

case $(uname -s) in
  Linux)
    BIN="$BIN" python3 "$(dirname "$0")/koshkit_evilfiles_sockets.py" 2> /dev/null
    ;;
  *) printf 'socket-fidelity=skipped-not-linux\n' ;;
esac
