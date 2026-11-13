#!/bin/sh
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
EXECUTABLE=$1
shift
exec gdb -ex "set sysroot $ROOT/root" -ex "set solib-search-path $ROOT/root" \
  "$ROOT/root/$EXECUTABLE" "$ROOT/dump/core" "$@"
