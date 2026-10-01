echo "== set --tab-selector value:"
"$BIN" --debug-complete-at 'set --tab-selector ' <"$TEST_NULL_DEVICE"
echo "== set --tab-selector= value:"
"$BIN" --debug-complete-at 'set --tab-selector=' <"$TEST_NULL_DEVICE"
echo "== set --tab-selector= prefix:"
"$BIN" --debug-complete-at 'set --tab-selector=e' <"$TEST_NULL_DEVICE"
echo "== kosh --tab-selector value:"
"$BIN" --debug-complete-at 'kosh --tab-selector ' <"$TEST_NULL_DEVICE"
echo "== kosh --tab-selector= prefix:"
"$BIN" --debug-complete-at 'kosh --tab-selector=p' <"$TEST_NULL_DEVICE"
echo "== the flag name itself:"
"$BIN" --debug-complete-at 'kosh --tab-s' <"$TEST_NULL_DEVICE"
echo "== set flag name:"
"$BIN" --debug-complete-at 'set --tab-s' <"$TEST_NULL_DEVICE"
echo "== an unrelated command keeps its own candidates:"
result=$("$BIN" --debug-complete-at 'echo --tab-selector ' <"$TEST_NULL_DEVICE") || exit 1
printf '%s\n' "$result" | grep -qxF interactive
[ "$?" -eq 1 ] || exit 1
printf '0\n'
