#!/bin/sh

#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.

set -eu

[ "$#" -eq 1 ] || { printf "Usage: %s FILENAME\n" "$0" >&2; exit 2; }
SOURCE=$1
NAME=${SOURCE##*/}
NAME=$(printf '%s' "${NAME%.*}" | LC_ALL=C tr 'a-z-' 'A-Z_')
case $NAME in
  ''|[0-9]*|*[!A-Z0-9_]*) printf 'Invalid script name: %s\n' "$SOURCE" >&2; exit 1 ;;
  *) ;;
esac

if grep -F -q ')KOSH_SCRIPT"' "$SOURCE"; then
  printf 'Raw literal delimiter occurs in %s\n' "$SOURCE" >&2
  exit 1
else
  STATUS=$?
  [ "$STATUS" -eq 1 ] || exit "$STATUS"
fi

printf 'inline constexpr StringView %s{R"KOSH_SCRIPT(' "$NAME"
cat -- "$SOURCE"
printf ')KOSH_SCRIPT"};\n\n'
