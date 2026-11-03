unset KOSH_FLAGS

directory=
trap 'test -n "$directory" && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT
directory=$(mktemp -d)

echo "== callback keeps definition diagnostics:"
completion_output=$("$BIN" --debug-complete-at 'vim /etc/' -c "cd '$directory' || exit; set -WW; _helper(){ eval 'printf \"%s\\n\" no_match_*'; }; _f(){ _helper >/dev/null; COMPREPLY=(ok); }; complete -F _f vim; set -M bash" </dev/null 2>"$directory/warnings")
if grep -q "glob pattern 'no_match_" "$directory/warnings"; then
    echo warning-leaked
else
    echo warning-clean
fi
printf '%s\n' "$completion_output"

echo "== callback keeps definition annoying state:"
"$BIN" --debug-complete-at 'annoyingcmd ' -c 'koshconf set optimizer.show_annoying_tier off; _f(){ COMPREPLY=("$(koshconf get optimizer.show_annoying_tier)"); }; complete -F _f annoyingcmd; koshconf set optimizer.show_annoying_tier on' </dev/null

echo "== callback keeps definition mood:"
"$BIN" --debug-complete-at 'moodcmd ' -c '_f(){ COMPREPLY=("$(set -M)"); }; complete -F _f moodcmd; set -M bash' </dev/null

echo "== word list keeps registration mood:"
"$BIN" -M bash-posix --debug-complete-at 'moodcmd ' -c "complete -W '\$(set -M)' moodcmd; set -M kosh" </dev/null

echo "== callback can replace its spec:"
"$BIN" -M bash --debug-complete-at 'selfcmd ' -c '_f(){ complete -W replacement selfcmd; COMPREPLY=(stable); }; complete -F _f selfcmd' </dev/null

test -n "$directory" && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"
directory=
