unset KOSH_FLAGS
# The caret line renders the argv[0] the harness invoked, which varies by host,
# so the test asserts the message text and the exit code, not the caret.
out=$("$BIN" --zz-bogus-flag 2>&1)
rc=$?
printf '%s\n' "$out" | grep -o "error: Unknown flag .*"
echo "rc=$rc"
# A long flag close to a known one, or missing only its qualifying word, names
# the known flag. A flag with no close match keeps the note about the separator.
for typed in --mimicry --enable-mimicy --zz-bogus-flag; do
  echo "== $typed"
  out=$("$BIN" "$typed" 2>&1)
  rc=$?
  printf '%s\n' "$out" | grep -E "^note:"
  echo "rc=$rc"
done
