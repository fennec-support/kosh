unset KOSH_FLAGS KOSHCONF
# koshconf messages say why a value is refused and where a setting moved, and
# --persist keeps a byte order mark, CRLF line ends, and a commented
# placeholder in place.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
export XDG_CONFIG_HOME="$work/config"
conf="$XDG_CONFIG_HOME/kosh/kosh.conf"
mkdir -p "$XDG_CONFIG_HOME/kosh"

do_quiet() {
  "$BIN" "$@" 2>&1 | grep -v '^ '
}

echo "== an invalid KOSHCONF names the reason:"
for blob in '%%%' AQ== gQA= BQEAAQEA; do
  KOSHCONF=$blob do_quiet -c :
  do_quiet -c "koshconf load $blob"
done
echo "== a Bash name points at its koshconf name:"
do_quiet -c 'koshconf get pipefail; koshconf set extglob on'
echo "== a retired set -o name points at its replacement:"
do_quiet --mood bash -c 'set -o koshkit; set +o no-diagnostics; set -o no-unset
set +o failglob'
echo "== an invalid kosh.init_moods names the bad entry:"
do_quiet -c 'koshconf set kosh.init_moods bash,fish'
echo "== enumeration values read naturally:"
do_quiet -c 'koshconf set kosh.editor.base_mode ed
koshconf set kosh.completion.add_space_after_completed_word true'
koshconf_help() {
  "$BIN" -c 'koshconf create bash' >/dev/null 2>&1
  grep -A1 -e "^# Insert a space" -e "^# Color the input" "$conf"
  "$BIN_DIR/invoke-koshkit" rm -- "$conf"
}
koshconf_help

echo "== --persist skips a byte order mark:"
printf '\357\273\277kosh.interpreter.glob_includes_dotfiles=off\n' >"$conf"
"$BIN" -c 'koshconf set kosh.interpreter.glob_includes_dotfiles on --persist'
od -An -c "$conf" | tr -s ' '
echo "== --persist keeps CRLF line ends:"
printf 'kosh.mood=bash\r\nkosh.editor.auto_close_brackets_and_quotes=off\r\n' >"$conf"
"$BIN" -c 'koshconf set kosh.editor.auto_close_brackets_and_quotes on --persist
koshconf set kosh.editor.history.max_entries 9 --persist'
od -An -c "$conf" | tr -s ' '
echo "== --persist fills a commented placeholder in place:"
printf '# Store history here.\n# kosh.editor.history.file_path=\nkosh.mood=bash\n' >"$conf"
"$BIN" -c 'koshconf set kosh.editor.history.file_path /tmp/h --persist'
cat "$conf"
