unset KOSH_FLAGS KOSHCONF
# koshconf lists the option registry, writes presets whose values match a fresh
# session, changes one option for the session or also in the file, and loads
# an encoded form while skipping an unknown id.
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

echo "== list prints every option with its class:"
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
grep '^mood=' "$conf"
echo "== the bash preset matches a fresh bash session:"
do_compare_preset bash
echo "== the sh preset matches a fresh sh session:"
"$BIN" -c 'koshconf create --force sh'
do_compare_preset sh
echo "== create writes every file option with its help and Bash name:"
grep -c '^legacy\.glob' "$conf"
grep -B2 '^legacy.glob_includes_dotfiles=' "$conf"
grep -c '^# history.file_path=$' "$conf"
grep -c '^legacy.privileged_mode=' "$conf"

echo "== set changes the session, and --persist rewrites one line:"
"$BIN" -c 'koshconf set editor.auto_close_brackets_and_quotes true; koshconf get editor.auto_close_brackets_and_quotes'
line_count=$(grep -c . "$conf")
"$BIN" -c 'koshconf set editor.auto_close_brackets_and_quotes 1 --persist'
grep '^editor.auto_close_brackets_and_quotes=' "$conf"
[ "$(grep -c . "$conf")" = "$line_count" ] && echo "same line count"
echo "== --persist appends a missing option and quotes a padded value:"
printf '# kept\nmood=bash\nmood=sh\n' >"$conf"
"$BIN" -c "koshconf set history.file_path ' padded ' --persist"
"$BIN" -c 'koshconf set mood kosh --persist'
cat "$conf"
echo "== --persist creates the file and its directory:"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
"$BIN" -c 'koshconf set completion.add_space_after_completed_word on --persist'
cat "$conf"
echo "== --persist writes an option that changes evaluation:"
"$BIN" -c 'koshconf set legacy.glob_includes_dotfiles on --persist'
echo "rc=$?"
grep '^legacy.glob_includes_dotfiles=' "$conf"
echo "== --persist rejects an option fixed by how the shell started:"
"$BIN" -c 'koshconf set legacy.privileged_mode off --persist'
echo "rc=$?"
echo "== a value the file cannot hold changes neither the session nor the file:"
KOSH_HISTORY_FILE=/history "$BIN" -c 'koshconf set history.file_path $'"'"'a\nb'"'"' --persist
echo "rc=$?"
koshconf get history.file_path'
grep -c '^history.file_path=' "$conf"
echo "== a failed write is a soft error that || can catch:"
chmod 500 "$XDG_CONFIG_HOME/kosh"
"$BIN" -c 'koshconf set editor.show_command_synopsis off --persist || echo "rc=$?"
koshconf get editor.show_command_synopsis
koshconf create --force kosh || echo "rc=$?"' 2>&1 | sed "s|$config|CONFIG|"
chmod 700 "$XDG_CONFIG_HOME/kosh"
echo "== string values are validated:"
"$BIN" -c 'koshconf set history.max_entries 12x
echo "rc=$?"
koshconf set history.max_entries -- -1
echo "rc=$?"
koshconf set history.file_path $'"'"'a\xffb'"'"'
echo "rc=$?"
koshconf set history.max_entries 0
koshconf get history.max_entries'
echo "== list escapes a value the file cannot hold:"
KOSH_HISTORY_FILE=$'a\nb\x01"\'' "$BIN" -c 'koshconf list |
  grep "^history.file_path="
echo "rc=$?"'

echo "== enumerations and booleans:"
"$BIN" -c 'koshconf set editor.completion_menu_style plain
koshconf get editor.completion_menu_style
koshconf set diagnostics.warning_level 2
echo "$-"
koshconf set legacy.exit_on_command_failure on
set -o | grep errexit
koshconf set legacy.base_editor_mode vi
koshconf get legacy.base_editor_mode
set -o | grep -E "^(emacs|vi) "
koshconf set legacy.base_editor_mode emacs
koshconf get legacy.base_editor_mode
set -o | grep -E "^(emacs|vi) "'

echo "== malformed forms and unknown names:"
"$BIN" -c 'koshconf'
echo "rc=$?"
"$BIN" -c 'koshconf bogus'
echo "rc=$?"
"$BIN" -c 'koshconf get nope'
echo "rc=$?"
"$BIN" -c 'koshconf set mood weird'
echo "rc=$?"
"$BIN" -c 'koshconf get mood --force'
echo "rc=$?"
"$BIN" -c 'koshconf create fish'
echo "rc=$?"

echo "== load applies an encoded form and skips an unknown id:"
"$BIN" -c 'koshconf load BQEByAEBAQ==; koshconf get editor.auto_close_brackets_and_quotes'
echo "rc=$?"
"$BIN" -c 'koshconf load "not base64"'
echo "rc=$?"
"$BIN" -c 'koshconf load BQ=='
echo "rc=$?"
echo "== load rejects a non-canonical encoding as a whole:"
for blob in hQABAQ== BYEAAQ== BQEBBQEA BgEABQEB BQEBBg; do
  "$BIN" -c "koshconf load $blob 2>/dev/null; echo \"$blob rc=\$?\"
koshconf get editor.auto_close_brackets_and_quotes"
done
echo "== load skips an invalid string value and keeps the rest:"
"$BIN" -c 'koshconf load BQEBCgF4; koshconf get editor.auto_close_brackets_and_quotes
koshconf get history.max_entries'

echo "== KOSHCONF carries the mood and interactive options to another shell:"
encoded=$("$BIN" -c 'koshconf set mood bash
koshconf set editor.completion_menu_style external
koshconf set history.max_entries 77
koshconf set legacy.exit_on_command_failure on
printf %s "$KOSHCONF"')
printf '%s\n' "$encoded"
KOSHCONF=$encoded "$BIN" -c 'koshconf get mood
koshconf get editor.completion_menu_style
koshconf get history.max_entries
koshconf get legacy.exit_on_command_failure'
"$BIN" -c "koshconf load '$encoded'; koshconf get editor.completion_menu_style"
echo "== KOSHCONF is never exported and a subshell sees the same value:"
"$BIN" -c 'env | grep -c "^KOSHCONF="
[ "$KOSHCONF" = "$( (printf %s "$KOSHCONF") & wait)" ] && echo same
KOSHCONF=ignored
[ "$KOSHCONF" != ignored ] && echo discarded'
echo "== an exported KOSHCONF carries the settings current at each child:"
"$BIN" -c 'export KOSHCONF
koshconf set mood bash
"$BIN" -c "koshconf get mood"
koshconf set editor.auto_close_brackets_and_quotes on
"$BIN" -c "koshconf get editor.auto_close_brackets_and_quotes" &
wait "$!"
[ "$(printenv KOSHCONF)" = "$KOSHCONF" ] && echo current'
