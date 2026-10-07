# koshconf completes its forms, the preset names, the registry option names,
# the values of an enumeration or a boolean, and its flags.
echo "== koshconf form:"
"$BIN" --debug-complete-at 'koshconf ' <"$TEST_NULL_DEVICE"
echo "== koshconf create preset:"
"$BIN" --debug-complete-at 'koshconf create ' <"$TEST_NULL_DEVICE"
echo "== koshconf get option prefix:"
"$BIN" --debug-complete-at 'koshconf get editor.' <"$TEST_NULL_DEVICE"
echo "== koshconf set option prefix after a flag:"
"$BIN" --debug-complete-at 'koshconf --persist set legacy.glob_no_' \
  <"$TEST_NULL_DEVICE"
echo "== koshconf set editor mode value:"
"$BIN" --debug-complete-at 'koshconf set legacy.base_editor_mode ' \
  <"$TEST_NULL_DEVICE"
echo "== koshconf set enumeration value:"
"$BIN" --debug-complete-at 'koshconf set mood ' <"$TEST_NULL_DEVICE"
echo "== koshconf set boolean value:"
"$BIN" --debug-complete-at 'koshconf set editor.show_command_synopsis o' <"$TEST_NULL_DEVICE"
echo "== koshconf flags:"
"$BIN" --debug-complete-at 'koshconf set --p' <"$TEST_NULL_DEVICE"
