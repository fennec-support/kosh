#!/bin/sh

directory=$(mktemp -d)
trap '[ -n "$directory" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT

for utility in cat cksum head sort tail uniq wc; do
    "$BIN" -c "koshkit $utility '$directory'" >/dev/null 2>&1
    printf '%s=%s\n' "$utility" "$?"
done

READ_DIRECTORY=$directory "$BIN" -c 'source "$READ_DIRECTORY"' \
    >/dev/null 2>&1
printf 'source=%s\n' "$?"

# The read builtin reports a failed read, from a directory or a closed
# descriptor, with status 1 and empty variables. Windows opens no directory for
# reading, so a write-only descriptor is the unreadable input there.
if [ "${OS-}" = Windows_NT ]; then
    unreadable="0> '$directory/write-only'"
else
    unreadable='< /'
fi
"$BIN" -c "line=kept; read -r line $unreadable"'
printf "status=%s line=[%s]\n" "$?" "$line"' 2>/dev/null
"$BIN" -c "read -r line $unreadable" 2>&1 >/dev/null |
    grep -c 'Unable to read input'
"$BIN" -c 'read -r -u 9 line; printf "status=%s\n" "$?"
read -r -n 1 byte '"$unreadable"'; printf "status=%s\n" "$?"' 2>/dev/null
