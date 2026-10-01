unset KOSH_FLAGS
# enable is accepted as a no-op since every builtin is always enabled in kosh.
# -a lists every builtin, named args return 0 for known builtins and 1 for
# unknown, and the flags -n, -f, -s are accepted without effect.
"$BIN" -c '
printf "== enable with no args returns 0:\n"
enable; printf "rc=%s\n" "$?"
printf "== enable compgen unset returns 0:\n"
enable compgen unset; printf "rc=%s\n" "$?"
printf "== enable -n echo returns 0:\n"
enable -n echo; printf "rc=%s\n" "$?"
'
echo "== enable -a lists builtins:"
enabled=$("$BIN" -c 'enable -a') || exit 1
for builtin_name in echo exit cd; do
  printf '%s\n' "$enabled" | grep -qxF "enable $builtin_name" || exit 1
  printf 'enable %s\n' "$builtin_name"
done
"$BIN" -c '
printf "== enable -f file echo returns 0:\n"
enable -f "$TEST_MKTEMP_DIRECTORY/enable-missing-builtin" echo
printf "rc=%s\n" "$?"
printf "== enable -s returns 0:\n"
enable -s; printf "rc=%s\n" "$?"
'
echo "== enable nosuchbuiltin reports and exits 1:"
"$BIN" -c 'enable nosuchbuiltin' 2>&1; echo "rc=$?"
echo "== builtin enable compgen unset forwards through enable:"
"$BIN" -c 'builtin enable compgen unset; echo ok'
