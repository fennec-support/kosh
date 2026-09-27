#!/bin/sh

output=$($BIN -c 'koshkit evildisk' 2>&1 >/dev/null)
case "$output" in
  ''|*Warning:\ Skipped\ *\ filesystem\ due\ to\ permission\ denied.|\
  *Warning:\ Skipped\ *\ filesystems\ due\ to\ permission\ denied.) classification=checked ;;
  *) classification=unexpected ;;
esac
printf 'permission-classification=%s\n' "$classification"
