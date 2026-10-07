# complete -E serves a tab on an empty line, and complete -I serves the
# command word in place of the command names, with -E first on an empty line.
# Like bash, a slot spec with no match offers nothing unless it has -o default
# or -o bashdefault, which fall back to the command names.
# A -D default spec lists its actions and word list like a command spec, and a
# loader that returns 124 restarts with the spec it registered. complete -r
# removes the named specs, a slot, or every spec, and complete -p prints the
# -I, -D, and -E lines and round-trips them. The probe runs in a temp
# directory holding one directory and one file.
dir=$(mktemp -d)
mkdir "$dir/slot_dir"
: > "$dir/slot_file"
echo "== -E completes an empty line:"
"$BIN" -c "complete -E -W 'empty_one empty_two'; complete -I -W 'initial_one'" --debug-complete-at '' </dev/null
echo "== -I completes the command word:"
"$BIN" -c "complete -E -W 'empty_one'; complete -I -W 'initial_one initial_two other'" --debug-complete-at 'init' </dev/null
echo "== -I completes the command word after a separator and an assignment:"
"$BIN" -c "complete -I -W 'initial_one initial_two'" --debug-complete-at 'echo x; v=1 initial_t' </dev/null
echo "== -I leaves an argument alone:"
"$BIN" -c "cd '$dir' || exit; complete -I -W 'initial_one'" --debug-complete-at 'cat slot_f' </dev/null
echo "== -I with no match offers nothing:"
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
  "$BIN" -c "complete -I -W 'initial_one'" --debug-complete-at 'compo' </dev/null
echo "== -I -o bashdefault with no match falls back to command names:"
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
  "$BIN" -c "complete -I -o bashdefault -W 'initial_one'" --debug-complete-at 'compo' </dev/null
echo "== the -E and -I functions receive the bash slot names:"
"$BIN" -c "f() { printf '[%s]' \"\$@\"; echo; COMPREPLY=(from_function); }; complete -E -F f; complete -I -F f" --debug-complete-at '' </dev/null
"$BIN" -c "f() { printf '[%s]' \"\$@\"; echo; COMPREPLY=(from_function); }; complete -I -F f" --debug-complete-at 'ab' </dev/null
echo "== -D lists its actions:"
"$BIN" -c "cd '$dir' || exit; complete -D -d" --debug-complete-at 'unknown_command slot' </dev/null
echo "== -D lists its word list with the prefix:"
"$BIN" -c "complete -D -W 'alpha beta' -P 'pre_'" --debug-complete-at 'unknown_command al' </dev/null
echo "== a -D loader returning 124 restarts with the loaded spec:"
"$BIN" -c "load() { complete -W 'loaded_one loaded_two' \"\$1\"; return 124; }; complete -D -F load -W 'default_word'" --debug-complete-at 'unknown_command lo' </dev/null
echo "== complete -r removes a named spec:"
"$BIN" -c "cd '$dir' || exit; complete -W 'word_one' cmd; complete -r cmd" --debug-complete-at 'cmd slot_f' </dev/null
echo "== complete -r removes one slot:"
"$BIN" -c "complete -W 'w' cmd; complete -E -W 'e'; complete -I -W 'i'; complete -r -E; complete -p cmd; complete -p -I; complete -p -E; echo status \$?" 2>/dev/null
echo "== complete -r with no name removes every spec:"
"$BIN" -c "complete -W 'w' cmd; complete -D -W 'd'; complete -E -W 'e'; complete -I -W 'i'; complete -r; complete -p; echo status \$?"
echo "== complete -r reports a missing spec and a missing slot:"
"$BIN" -c "complete -W 'w' cmd; complete -r missing_command; echo status \$?; complete -r -E; echo status \$?" 2>/dev/null
echo "== -D wins over -E, and -E over -I:"
"$BIN" -c "complete -E -I -W 'e'; complete -D -E -W 'd'; complete -p"
echo "== complete -p prints the slots and round-trips:"
"$BIN" -c "complete -I -o nospace -c; complete -D -o default -F load; complete -E -W 'e1 e2'
printed=\$(complete -p)
echo \"\$printed\"
complete -p -I -E
complete -p -E
complete -r
eval \"\$printed\"
[ \"\$(complete -p)\" = \"\$printed\" ] && echo round-trip-ok"
echo "== complete -p on a missing slot fails:"
"$BIN" -c "complete -p -I; echo status \$?" 2>/dev/null
cd /
rm -rf "$dir"
