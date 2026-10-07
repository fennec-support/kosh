#!/bin/sh
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This script verifies time keyword and builtin formatting, resident memory
# reporting, runtime mood changes, and explicit TIMEFORMAT values.

unset KOSH_FLAGS

report_shape()
{
  printf '%s\n' "$1" | while IFS= read -r line; do
    case $line in
      ''|custom) prefix=$line ;;
      *[0-9]*)
        prefix=${line%%[0-9]*}
        numeric_tail=${line#"$prefix"}
        case $numeric_tail in
          *[!0-9m.s%MKGTPB]*) exit 1 ;;
        esac
        ;;
      *) exit 1 ;;
    esac
    printf '[%s]' "$prefix"
  done
  [ "$?" -eq 0 ] || return 1
  printf '\n'
}

report=$("$BIN" --no-init-files --mood bash -c \
  'time "$1" --no-init-files -c :; set -M kosh; time "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1) || exit 1
shape=$(report_shape "$report") || exit 1
echo "runtime-mood=$shape"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'TIMEFORMAT=""; time -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1) || exit 1
shape=$(report_shape "$report") || exit 1
echo "kosh-rss=$shape"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'TIMEFORMAT=""; time -R true' 2>&1) || exit 1
shape=$(report_shape "$report") || exit 1
echo "zero-rss=$shape"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'TIMEFORMAT=""; builtin time -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1) || exit 1
shape=$(report_shape "$report") || exit 1
echo "builtin-rss=$shape"

report=$("$BIN" --no-init-files --no-diagnostics -c \
  'time -p -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1) || exit 1
shape=$(report_shape "$report") || exit 1
echo "posix=$shape"

report=$("$BIN" --no-init-files --mood bash -c \
  'TIMEFORMAT=custom; time -R "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1) || exit 1
shape=$(report_shape "$report") || exit 1
echo "bash-custom=$shape"

report=$("$BIN" --no-init-files --mood bash -c \
  'builtin time "$1" --no-init-files -c :' \
  time-test "$BIN" 2>&1) || exit 1
shape=$(report_shape "$report") || exit 1
echo "bash-builtin=$shape"
