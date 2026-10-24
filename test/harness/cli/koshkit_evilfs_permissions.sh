#!/bin/sh

report=$("$BIN" -c 'koshkit --color never evilfs --all' 2>&1)

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

disk_report=$("$BIN" -c 'koshkit evildisk' 2>&1 >/dev/null)
case $disk_report in
  *'warning:'*)
    if printf '%s\n' "$disk_report" |
      grep -Eq '^warning: Skipped [1-9][0-9]* filesystems? due to permission denied\.$'
    then
      disk_warning_shape=checked
    else
      disk_warning_shape=invalid
    fi
    ;;
  *)
    disk_warning_shape=checked
    ;;
esac
printf 'evildisk-permission-warning=%s\n' "$disk_warning_shape"
