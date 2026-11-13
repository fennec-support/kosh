#!/bin/sh
ROOT=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P) || exit 1
EXECUTABLE=$1
shift
exec lldb -O "platform select host --sysroot '$ROOT/root'" \
  --core "$ROOT/dump/core" "$ROOT/root/$EXECUTABLE" "$@"
