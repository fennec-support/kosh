unset KOSH_FLAGS
home=$(mktemp -d)
trap '[ -n "$home" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$home"' EXIT
printf 'echo RC-MARK\nset --init-moods kosh,bash\n' > "$home/.koshrc"
printf 'arr=(a b c)\necho "bashrc-arr=${arr[1]}"\n' > "$home/.bashrc"
HOME="$home" "$BIN" -i <"$TEST_NULL_DEVICE" >"$home/out" 2>&1
status=$?
rc_mark_count=$(grep -c '^RC-MARK$' "$home/out")
bashrc_array_count=$(grep -c '^bashrc-arr=b$' "$home/out")
[ "$status" -eq 1 ] || exit 1
[ "$rc_mark_count" -eq 1 ] || exit 1
[ "$bashrc_array_count" -eq 1 ] || exit 1
printf 'exit=%s\nrc-mark=%s\nbashrc-array=%s\n' \
  "$status" "$rc_mark_count" "$bashrc_array_count"
