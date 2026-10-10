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

# Arithmetic expansion shares the parameter-expansion cap, so a deep $(( nest
# is a located error too.
deep_open=$(printf '$((%.0s' $(seq 1 600))
deep_close=$(printf '))%.0s' $(seq 1 600))
deep="${deep_open}1${deep_close}"

echo "== deep arithmetic nesting is guarded, not crashing:"
"$BIN" --mood bash -c "echo $deep" 2>&1 | grep -c "nested too deeply"

echo "== exit status is a clean error, not a signal death:"
"$BIN" --mood bash -c "echo $deep" >/dev/null 2>&1
status=$?
[ "$status" -lt 128 ] && echo ok || echo "crashed with signal $((status - 128))"

echo "== shallow arithmetic nesting still evaluates:"
"$BIN" --mood bash -c 'echo $((1 + $((2 * $((3))))))'

# A function body that fails to parse drops the here-document it registered,
# so the recovery collects no body into the freed body storage.
echo "== a failed function body with a here-document reports a syntax error:"
printf 'p(){ cat <<E; fi; }\nx\nE\n' > "$TEST_TEMP_DIRECTORY/deep-heredoc-body.sh"
"$BIN" -n "$TEST_TEMP_DIRECTORY/deep-heredoc-body.sh" > /dev/null 2>&1
echo "status=$?"
