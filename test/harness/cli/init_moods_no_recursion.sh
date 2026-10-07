unset KOSH_FLAGS
# A set -L inside the bash rc names the moods already sourcing, so the rc runs
# once under the bash grammar instead of recursing.
home=$(mktemp -d)
trap '[ -n "$home" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$home"' EXIT
printf 'echo RC-MARK\narr=(a b c)\necho "bashrc-arr=${arr[1]}"\nset -L kosh,bash\n' \
  > "$home/.bashrc"
HOME="$home" "$BIN" -L bash -i <"$TEST_NULL_DEVICE" >"$home/out" 2>&1
status=$?
rc_mark_count=$(grep -c '^RC-MARK$' "$home/out")
bashrc_array_count=$(grep -c '^bashrc-arr=b$' "$home/out")
[ "$status" -eq 1 ] || exit 1
[ "$rc_mark_count" -eq 1 ] || exit 1
[ "$bashrc_array_count" -eq 1 ] || exit 1
printf 'exit=%s\nrc-mark=%s\nbashrc-array=%s\n' \
  "$status" "$rc_mark_count" "$bashrc_array_count"
