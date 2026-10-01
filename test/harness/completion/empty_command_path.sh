dir=$(mktemp -d)
trap '[ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
if [ "${OS-}" = Windows_NT ]; then
  executable_suffix=.exe
else
  executable_suffix=
fi
"$BIN_DIR/invoke-koshkit" cp "$BIN" "$dir/path_probe$executable_suffix"

empty_result=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-complete-at '' <"$TEST_NULL_DEVICE")
case "$empty_result" in
    *path_probe*) ;;
    *) exit 1 ;;
esac
echo "== empty command includes PATH programs"

segment_result=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-complete-at 'true; ' <"$TEST_NULL_DEVICE")
case "$segment_result" in
    *path_probe*) ;;
    *) exit 1 ;;
esac
echo "== empty command segment includes PATH programs"
