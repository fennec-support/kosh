# A deeply nested command substitution is capped with a located error rather than
# overflowing the native stack, so the shell reports the depth and stays alive.
# The depth here is past the substitution cap, while a shallow nesting still runs.
deep_open=$(printf '$(echo %.0s' $(seq 1 300))
deep_close=$(printf ')%.0s' $(seq 1 300))
deep="${deep_open}deep${deep_close}"

echo "== deep nesting is guarded, not crashing:"
"$BIN" --mood bash -c "echo $deep" 2>&1 | grep -c "nested too deeply"

echo "== exit status is a clean error, not a signal death:"
"$BIN" --mood bash -c "echo $deep" >/dev/null 2>&1
status=$?
[ "$status" -lt 128 ] && echo ok || echo "crashed with signal $((status - 128))"

echo "== shallow nesting still works:"
"$BIN" --mood bash -c 'echo $(echo $(echo hi))'

# A deeply nested parameter expansion default is capped with a located error
# rather than overflowing the native stack, so the shell reports the depth and
# stays alive. The depth here is past the parameter-expansion cap, while a
# shallow nesting still expands.
deep_open=$(printf '${x:-%.0s' $(seq 1 600))
deep_close=$(printf '}%.0s' $(seq 1 600))
deep="${deep_open}z${deep_close}"

echo "== deep nesting is guarded, not crashing:"
"$BIN" --mood bash -c "echo $deep" 2>&1 | grep -c "nested too deeply"

echo "== exit status is a clean error, not a signal death:"
"$BIN" --mood bash -c "echo $deep" >/dev/null 2>&1
status=$?
[ "$status" -lt 128 ] && echo ok || echo "crashed with signal $((status - 128))"

echo "== shallow nesting still expands:"
"$BIN" --mood bash -c 'echo "${x:-${y:-hi}}"'

# A function body that fails to parse drops the here-document it registered,
# so the recovery collects no body into the freed body storage.
echo "== a failed function body with a here-document reports a syntax error:"
printf 'p(){ cat <<E; fi; }\nx\nE\n' > "$TEST_TEMP_DIRECTORY/deep-heredoc-body.sh"
"$BIN" -n "$TEST_TEMP_DIRECTORY/deep-heredoc-body.sh" > /dev/null 2>&1
echo "status=$?"

# A long right-associative '**' chain is capped with an error, and the shell
# survives it.
chain=$(printf '2**%.0s' $(seq 1 100000))
printf 'echo $(( %s1 ))\n' "$chain" > "$TEST_TEMP_DIRECTORY/deep-power-chain.sh"
echo "== a long power chain is guarded, not crashing:"
"$BIN" "$TEST_TEMP_DIRECTORY/deep-power-chain.sh" 2>&1 | grep -c "nested too deeply"
"$BIN" "$TEST_TEMP_DIRECTORY/deep-power-chain.sh" >/dev/null 2>&1
status=$?
[ "$status" -lt 128 ] && echo ok || echo "crashed with signal $((status - 128))"

# A conditional nested in its else branches recurses the same way.
chain=$(printf '1?1:%.0s' $(seq 1 10000))
printf 'echo $(( %s1 ))\n' "$chain" > "$TEST_TEMP_DIRECTORY/deep-conditional.sh"
echo "== a long conditional chain is guarded, not crashing:"
"$BIN" "$TEST_TEMP_DIRECTORY/deep-conditional.sh" 2>&1 | grep -c "nested too deeply"

# Command substitutions nested inside double quotes are scanned recursively,
# and the scan stops at the substitution cap.
quoted=$(printf '"$(%.0s' $(seq 1 20000))
printf 'echo %s\n' "$quoted" > "$TEST_TEMP_DIRECTORY/deep-quoted-substitution.sh"
echo "== deep quoted substitution nesting is a syntax error:"
"$BIN" -n "$TEST_TEMP_DIRECTORY/deep-quoted-substitution.sh" 2>&1 |
  grep -c "nested too deeply"
"$BIN" -n "$TEST_TEMP_DIRECTORY/deep-quoted-substitution.sh" >/dev/null 2>&1
echo "status=$?"

# Unbounded recursion through a function body ends with an error before the
# native stack overflows, and the shell survives it.
printf 'f() { [ "$1" -ge 0 ] && f $(( $1 + 1 )); }\nf 0\n' \
  > "$TEST_TEMP_DIRECTORY/deep-function-recursion.sh"
echo "== unbounded function recursion is guarded, not crashing:"
"$BIN" "$TEST_TEMP_DIRECTORY/deep-function-recursion.sh" 2>&1 |
  grep -c "nested too deeply"
"$BIN" "$TEST_TEMP_DIRECTORY/deep-function-recursion.sh" >/dev/null 2>&1
status=$?
[ "$status" -lt 128 ] && echo ok || echo "crashed with signal $((status - 128))"
