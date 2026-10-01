unset KOSH_FLAGS
# The exit builtin masks an out-of-range status to its low 8 bits, so exit 300
# in a subshell reports 44.
echo "== exit masks an out-of-range status to 8 bits in a subshell:"
"$BIN" --no-diagnostics -c '(exit 300); echo "$?"' 2>"$TEST_NULL_DEVICE"
"$BIN" --no-diagnostics -c 'exit 54' >"$TEST_NULL_DEVICE" 2>&1
printf 'explicit-exit=%s\n' "$?"
"$BIN" --no-diagnostics -c 'false; exit' >"$TEST_NULL_DEVICE" 2>&1
printf 'bare-exit=%s\n' "$?"
