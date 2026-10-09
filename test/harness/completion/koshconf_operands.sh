# koshconf completes its forms, the preset names, the registry option names
# by prefix and then by subsequence, the values of an enumeration, a boolean,
# and the mood list, no file for a count, and only the flags of the form.
echo "== koshconf form:"
"$BIN" --debug-complete-at 'koshconf ' <"$TEST_NULL_DEVICE"
echo "== koshconf create preset:"
"$BIN" --debug-complete-at 'koshconf create ' <"$TEST_NULL_DEVICE"
echo "== koshconf get option prefix:"
"$BIN" --debug-complete-at 'koshconf get editor.' <"$TEST_NULL_DEVICE"
echo "== koshconf set option prefix after a flag:"
"$BIN" --debug-complete-at 'koshconf --persist set kosh.glob_no_' \
  <"$TEST_NULL_DEVICE"
echo "== koshconf set editor mode value:"
"$BIN" --debug-complete-at 'koshconf set editor.base_mode ' \
  <"$TEST_NULL_DEVICE"
echo "== koshconf set enumeration value:"
"$BIN" --debug-complete-at 'koshconf set kosh.mood ' <"$TEST_NULL_DEVICE"
echo "== koshconf set boolean value:"
"$BIN" --debug-complete-at 'koshconf set editor.hints.show_command_synopsis o' <"$TEST_NULL_DEVICE"
echo "== koshconf flags:"
"$BIN" --debug-complete-at 'koshconf set --p' <"$TEST_NULL_DEVICE"
echo "== koshconf set init moods value:"
"$BIN" --debug-complete-at 'koshconf set kosh.init_moods ' \
  <"$TEST_NULL_DEVICE"
echo "== koshconf set init moods after a comma:"
"$BIN" --debug-complete-at 'koshconf set kosh.init_moods kosh,ba' \
  <"$TEST_NULL_DEVICE"
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: >"$dir/history-probe"
cd "$dir" || exit 1
echo "== koshconf set count value offers no file:"
"$BIN" --debug-complete-at 'koshconf set editor.history.max_entries ' \
  <"$TEST_NULL_DEVICE"
echo "== koshconf set path value offers files:"
"$BIN" --debug-complete-at 'koshconf set editor.history.file_path hist' \
  <"$TEST_NULL_DEVICE"
echo "== koshconf set flags after the value:"
"$BIN" --debug-complete-at 'koshconf set kosh.mood bash -' <"$TEST_NULL_DEVICE"
echo "== koshconf create flags:"
"$BIN" --debug-complete-at 'koshconf create bash -' <"$TEST_NULL_DEVICE"
echo "== koshconf option name by subsequence:"
"$BIN" --debug-complete-at 'koshconf set maxent' <"$TEST_NULL_DEVICE"
echo "== koshconf option name prefix wins over subsequence:"
"$BIN" --debug-complete-at 'koshconf get kosh.mood' <"$TEST_NULL_DEVICE"
