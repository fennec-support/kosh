unset KOSH_FLAGS KOSHCONF
# koshconf lists the option registry, writes presets whose values match a fresh
# session, changes one option for the session or also in the file, suggests a
# close name for an unknown one, and loads an encoded form while skipping an
# unknown id.
config=$(mktemp -d)
trap '[ -n "$config" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$config"' EXIT
export XDG_CONFIG_HOME="$config/user"
conf="$XDG_CONFIG_HOME/kosh/kosh.conf"

do_compare_preset() {
  local mood=$1
  local name value actual
  while IFS='=' read -r name value
  do
    case $name in
      '#'* | '') continue ;;
    esac
    actual=$(XDG_CONFIG_HOME="$config/empty" "$BIN" --mood "$mood" \
      -c "koshconf get $name")
    [ "$actual" = "$value" ] || echo "mismatch in $mood: $name=$value, got $actual"
  done <"$conf"
}

echo "== list prints every option as name=value:"
KOSH_HISTORY_FILE=/history "$BIN" -c 'koshconf list'
echo "== the kosh preset matches a fresh kosh session:"
"$BIN" -c 'koshconf create kosh'
echo "rc=$?"
cat "$conf"
do_compare_preset kosh
echo "== create refuses an existing file without --force:"
"$BIN" -c 'koshconf create bash' 2>&1 | sed "s|$config|CONFIG|"
echo "rc=${PIPESTATUS[0]}"
"$BIN" -c 'koshconf create --force bash'
echo "rc=$?"
grep '^kosh.mood=' "$conf"
echo "== the bash preset matches a fresh bash session:"
do_compare_preset bash
echo "== the bash preset leaves the alias default to the session:"
grep -B2 '^# kosh.interpreter.aliases_expand=$' "$conf"
"$BIN" -c 'shopt -p expand_aliases'
echo "== the sh preset matches a fresh sh session:"
"$BIN" -c 'koshconf create --force sh'
do_compare_preset sh
echo "== create writes every file option with its help and Bash name:"
grep -c '^kosh\.interpreter\.glob' "$conf"
grep -B2 '^kosh.interpreter.glob_includes_dotfiles=' "$conf"
grep -c '^# kosh.editor.history.file_path=$' "$conf"
grep -c '^kosh.interpreter.privileged_mode=' "$conf"
echo "== a Bash spelling stays on one comment line:"
grep -c -E '\((set -o|shopt) [a-z_]*$' "$conf"
grep -c -E '^# [a-z_]+\)$' "$conf"
grep -B1 '^kosh.interpreter.assignments_anywhere_in_command=' "$conf"

echo "== set changes the session, and --persist rewrites one line:"
"$BIN" -c 'koshconf set kosh.editor.auto_close_brackets_and_quotes true; koshconf get kosh.editor.auto_close_brackets_and_quotes'
line_count=$(grep -c . "$conf")
"$BIN" -c 'koshconf set kosh.editor.auto_close_brackets_and_quotes 1 --persist'
grep '^kosh.editor.auto_close_brackets_and_quotes=' "$conf"
[ "$(grep -c . "$conf")" = "$line_count" ] && echo "same line count"
echo "== --persist appends a missing option and quotes a padded value:"
printf '# kept\nkosh.mood=bash\nkosh.mood=sh\n' >"$conf"
"$BIN" -c "koshconf set kosh.editor.history.file_path ' padded ' --persist"
"$BIN" -c 'koshconf set kosh.mood kosh --persist'
cat "$conf"
echo "== --persist creates the file and its directory:"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
"$BIN" -c 'koshconf set kosh.completion.add_space_after_completed_word on --persist'
cat "$conf"
echo "== --persist writes an option that changes evaluation:"
"$BIN" -c 'koshconf set kosh.interpreter.glob_includes_dotfiles on --persist'
echo "rc=$?"
grep '^kosh.interpreter.glob_includes_dotfiles=' "$conf"
echo "== --persist rejects an option fixed by how the shell started:"
"$BIN" -c 'koshconf set kosh.interpreter.privileged_mode off --persist'
echo "rc=$?"
echo "== a value the file cannot hold changes neither the session nor the file:"
KOSH_HISTORY_FILE=/history "$BIN" -c 'koshconf set kosh.editor.history.file_path $'"'"'a\nb'"'"' --persist
echo "rc=$?"
koshconf get kosh.editor.history.file_path'
grep -c '^kosh.editor.history.file_path=' "$conf"
echo "== a failed write is a soft error that || can catch:"
chmod 500 "$XDG_CONFIG_HOME/kosh"
"$BIN" -c 'koshconf set kosh.editor.hints.show_command_synopsis off --persist || echo "rc=$?"
koshconf get kosh.editor.hints.show_command_synopsis
koshconf create --force kosh || echo "rc=$?"' 2>&1 | sed "s|$config|CONFIG|"
chmod 700 "$XDG_CONFIG_HOME/kosh"
echo "== string values are validated:"
"$BIN" -c 'koshconf set kosh.editor.history.max_entries 12x
echo "rc=$?"
koshconf set kosh.editor.history.max_entries -- -1
echo "rc=$?"
koshconf set kosh.editor.history.max_entries 99999999999999999999
echo "rc=$?"
koshconf set kosh.editor.history.max_entries 2147483648
echo "rc=$?"
koshconf set kosh.editor.history.max_entries 2147483647
echo "rc=$?"
koshconf set kosh.editor.history.file_path $'"'"'a\xffb'"'"'
echo "rc=$?"
koshconf set kosh.editor.history.max_entries 0
koshconf get kosh.editor.history.max_entries'
echo "== list escapes a value the file cannot hold:"
KOSH_HISTORY_FILE=$'a\nb\x01"\'' "$BIN" -c 'koshconf list |
  grep "^kosh.editor.history.file_path="
echo "rc=$?"'

echo "== enumerations and booleans:"
"$BIN" -c 'koshconf set kosh.completion.menu_style plain
koshconf get kosh.completion.menu_style
koshconf set kosh.optimizer.warning_level 2
echo "$-"
koshconf set kosh.interpreter.exit_on_command_failure on
set -o | grep errexit
koshconf set kosh.editor.base_mode vi
koshconf get kosh.editor.base_mode
set -o | grep -E "^(emacs|vi) "
koshconf set kosh.editor.base_mode emacs
koshconf get kosh.editor.base_mode
set -o | grep -E "^(emacs|vi) "'

echo "== malformed forms and unknown names:"
"$BIN" -c 'koshconf'
echo "rc=$?"
"$BIN" -c 'koshconf bogus'
echo "rc=$?"
"$BIN" -c 'koshconf get nope'
echo "rc=$?"
"$BIN" -c 'koshconf set kosh.mood weird'
echo "rc=$?"
"$BIN" -c 'koshconf get kosh.mood --force'
echo "rc=$?"
"$BIN" -c 'koshconf create fish'
echo "rc=$?"
echo "== an unknown name or form suggests a close one:"
"$BIN" -c 'koshconf sett kosh.mood bash'
echo "rc=$?"
"$BIN" -c 'koshconf get kosh.editor.history.max_entry'
echo "rc=$?"
"$BIN" -c 'koshconf get max_entries'
echo "rc=$?"
echo "== a negative value is a value, not a flag:"
"$BIN" -c 'koshconf set kosh.editor.history.max_entries -5'
echo "rc=$?"

echo "== load applies an encoded form and skips an unknown id:"
"$BIN" -c 'koshconf load BQEByAEBAQ==; koshconf get kosh.editor.auto_close_brackets_and_quotes'
echo "rc=$?"
"$BIN" -c 'koshconf load "not base64"'
echo "rc=$?"
"$BIN" -c 'koshconf load BQ=='
echo "rc=$?"
echo "== load rejects a non-canonical encoding as a whole:"
for blob in hQABAQ== BYEAAQ== BQEBBQEA BgEABQEB BQEBBg; do
  "$BIN" -c "koshconf load $blob 2>/dev/null; echo \"$blob rc=\$?\"
koshconf get kosh.editor.auto_close_brackets_and_quotes"
done
echo "== load skips an invalid string value and keeps the rest:"
"$BIN" -c 'koshconf load BQEBCgF4; koshconf get kosh.editor.auto_close_brackets_and_quotes
koshconf get kosh.editor.history.max_entries'

echo "== KOSHCONF carries the mood and interactive options to another shell:"
encoded=$("$BIN" -c 'koshconf set kosh.mood bash
koshconf set kosh.completion.menu_style external
koshconf set kosh.editor.history.max_entries 77
koshconf set kosh.interpreter.exit_on_command_failure on
printf %s "$KOSHCONF"')
printf '%s\n' "$encoded"
KOSHCONF=$encoded "$BIN" -c 'koshconf get kosh.mood
koshconf get kosh.completion.menu_style
koshconf get kosh.editor.history.max_entries
koshconf get kosh.interpreter.exit_on_command_failure'
"$BIN" -c "koshconf load '$encoded'; koshconf get kosh.completion.menu_style"
echo "== KOSHCONF is never exported and a subshell sees the same value:"
"$BIN" -c 'env | grep -c "^KOSHCONF="
[ "$KOSHCONF" = "$( (printf %s "$KOSHCONF") & wait)" ] && echo same
KOSHCONF=ignored
[ "$KOSHCONF" != ignored ] && echo discarded'
echo "== an exported KOSHCONF carries the settings current at each child:"
"$BIN" -c 'export KOSHCONF
koshconf set kosh.mood bash
"$BIN" -c "koshconf get kosh.mood"
koshconf set kosh.editor.auto_close_brackets_and_quotes on
"$BIN" -c "koshconf get kosh.editor.auto_close_brackets_and_quotes" &
wait "$!"
[ "$(printenv KOSHCONF)" = "$KOSHCONF" ] && echo current'
