#!/bin/sh

report=$("$BIN" -c 'koshkit --color never evilfs --all' 2>&1)

case $report in
  *'SOURCE'*'OPTIONS'*) detail_shape=matched ;;
  *) detail_shape=missing ;;
esac
printf 'detail-shape=%s\n' "$detail_shape"

case $report in
  *'warning:'*)
    if printf '%s\n' "$report" |
      grep -Eq '^warning: Skipped [1-9][0-9]* filesystems? due to permission denied\.$'
    then
      warning_shape=checked
    else
      warning_shape=invalid
    fi
    ;;
  *)
    warning_shape=checked
    ;;
esac
printf 'permission-warning=%s\n' "$warning_shape"
