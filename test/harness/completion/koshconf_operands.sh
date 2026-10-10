# koshconf completes its forms, the preset names, the registry option names
# by prefix and then by subsequence, the values of an enumeration, a boolean,
# and the mood list, no file for a count, and only the flags of the form.
echo "== koshconf form:"
"$BIN" --debug-complete-at 'koshconf ' </dev/null
echo "== koshconf create preset:"
"$BIN" --debug-complete-at 'koshconf create ' </dev/null
echo "== koshconf get option prefix:"
"$BIN" --debug-complete-at 'koshconf get editor.' </dev/null
echo "== koshconf set option prefix after a flag:"
"$BIN" --debug-complete-at 'koshconf --persist set interpreter.bash.glob_no_' \
  </dev/null
echo "== koshconf set editor mode value:"
"$BIN" --debug-complete-at 'koshconf set editor.base_mode ' \
  </dev/null
echo "== koshconf set enumeration value:"
"$BIN" --debug-complete-at 'koshconf set mood ' </dev/null
echo "== koshconf set boolean value:"
"$BIN" --debug-complete-at 'koshconf set editor.hints.show_command_synopsis o' </dev/null
echo "== koshconf flags:"
"$BIN" --debug-complete-at 'koshconf set --p' </dev/null
echo "== koshconf set init moods value:"
"$BIN" --debug-complete-at 'koshconf set init_moods ' \
  </dev/null
echo "== koshconf set init moods after a comma:"
"$BIN" --debug-complete-at 'koshconf set init_moods kosh,ba' \
  </dev/null
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
: >"$dir/history-probe"
cd "$dir" || exit 1
echo "== koshconf set count value offers no file:"
"$BIN" --debug-complete-at 'koshconf set editor.history.max_entries ' \
  </dev/null
echo "== koshconf set path value offers files:"
"$BIN" --debug-complete-at 'koshconf set editor.history.file_path hist' \
  </dev/null
echo "== koshconf set flags after the value:"
"$BIN" --debug-complete-at 'koshconf set mood bash -' </dev/null
echo "== koshconf create flags:"
"$BIN" --debug-complete-at 'koshconf create bash -' </dev/null
echo "== koshconf option name by subsequence:"
"$BIN" --debug-complete-at 'koshconf set maxent' </dev/null
echo "== koshconf option name prefix wins over subsequence:"
"$BIN" --debug-complete-at 'koshconf get mood' </dev/null
echo "== koshconf get offers the legacy Bash alias:"
"$BIN" --debug-complete-at 'koshconf get legacy.bash.extg' \
  </dev/null
echo "== koshconf set offers the legacy POSIX alias:"
"$BIN" --debug-complete-at 'koshconf set legacy.posix.errex' \
  </dev/null
