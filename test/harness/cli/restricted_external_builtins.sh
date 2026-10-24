unset KOSH_FLAGS KOSHCONF

echo "== newgrp is blocked:"
PATH=/usr/bin:/bin "$BIN" -r -c 'newgrp; echo "status=$?"' 2>&1

echo "== bench is blocked:"
PATH=/usr/bin:/bin "$BIN" -r -c 'bench --no-shell /bin/echo; echo "status=$?"' 2>&1

echo "== assimilate is blocked:"
PATH=/usr/bin:/bin "$BIN" -r -c 'assimilate root@example.com; echo "status=$?"' 2>&1

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'echo sourced\n' >"$work/payload"

echo "== source without a slash searches PATH:"
env -u PATH \
  "$TEST_PATH_ENVIRONMENT_NAME=$work${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" \
  "$BIN" -r -c 'source payload; echo "status=$?"' 2>&1

echo "== dot with a slash is blocked:"
(cd "$work" && PATH=/usr/bin:/bin "$BIN" -r -c '. ./payload; echo "status=$?"' 2>&1)

# The excerpt of an absolute path depends on the temporary directory, so the
# located message is counted.
echo "== dot with an absolute path is blocked:"
absolute_output=$(PATH=/usr/bin:/bin "$BIN" -r -c ". '$work/payload'" 2>&1)
absolute_status=$?
echo "status=$absolute_status located=$(printf '%s\n' "$absolute_output" |
  grep -c 'Source paths containing a directory separator are forbidden')"

echo "== rescue mode keeps the shell restricted:"
printf 'cd /\n' | "$BIN" -c 'exec -a -kosh "$1" -r --bogus' kosh "$BIN" 2>&1 |
  grep -c 'Changing the directory is forbidden in a restricted shell'
