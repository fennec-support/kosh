unset KOSH_FLAGS

echo "== bench points at the failing command, not the whole bench line:"
result=$("$BIN" -c 'bench --runs 1 ls --color' 2>&1)
[ "$?" -eq 1 ] || exit 1
printf '%s\n' "$result"
echo "== kill points at the bad pid, not the whole kill line:"
result=$("$BIN" -c 'kill notapid' 2>&1)
[ "$?" -eq 1 ] || exit 1
printf '%s\n' "$result"
echo "== break points at the bad count, not the whole break line:"
result=$("$BIN" -c 'for i in 1 2; do break 0; done' 2>&1)
[ "$?" -eq 1 ] || exit 1
printf '%s\n' "$result"
echo "== exit points at the bad status, not the whole exit line:"
result=$("$BIN" --no-diagnostics -c 'exit notanumber' 2>&1)
[ "$?" -eq 2 ] || exit 1
printf '%s\n' "$result"
echo "== umask points at the bad mask, not the whole umask line:"
result=$("$BIN" -c 'umask 0999' 2>&1)
[ "$?" -eq 1 ] || exit 1
printf '%s\n' "$result"
echo "== set points at the bad option, not the whole set line:"
result=$("$BIN" -c 'set -Z' 2>&1)
[ "$?" -eq 1 ] || exit 1
printf '%s\n' "$result"
echo "== z points at the bad query, not the whole z line:"
result=$("$BIN" -c 'z no_such_dir_xyz' 2>&1)
[ "$?" -eq 1 ] || exit 1
printf '%s\n' "$result"
