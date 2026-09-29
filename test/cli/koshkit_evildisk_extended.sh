#!/bin/sh

path=$TEST_TEMP_DIRECTORY/evildisk-file
: > "$path"
report=$($BIN -c 'koshkit --color never evildisk --all "$1"' evildisk "$path" \
  2>/dev/null)
case $report in
  *'Unavailable sections'*'SMART data'*|*'unavailable on this platform'*|*'are unavailable'*|*'is unavailable'*)
    extended_shape=matched
    ;;
  *) extended_shape=wrong ;;
esac
printf 'extended-shape=%s\n' "$extended_shape"

default_report=$($BIN -c 'koshkit --color never evildisk "$1"' evildisk "$path" \
  2>/dev/null)
case $default_report in
  *IDENTITY*|*"FILESYSTEM FAILURES"*) default_scope=extra ;;
  *) default_scope=capacity ;;
esac
printf 'default-scope=%s\n' "$default_scope"

first_path=$TEST_TEMP_DIRECTORY/evildisk-z
second_path=$TEST_TEMP_DIRECTORY/evildisk-a
: > "$first_path"
: > "$second_path"
ordered_report=$("$BIN" -c 'koshkit --color never evildisk "$1" "$2"' \
  evildisk "$first_path" "$second_path" 2>/dev/null)
case $ordered_report in
  *"$first_path"*"$second_path"*) operand_order=preserved ;;
  *) operand_order=changed ;;
esac
printf 'operand-order=%s\n' "$operand_order"
