unset KOSH_FLAGS KOSHCONF
# koshconf messages say why a value is refused and where a setting moved, and
# --persist keeps a byte order mark, CRLF line ends, and a commented
# placeholder in place. Startup ignores an invalid KOSHCONF and continues.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
export XDG_CONFIG_HOME="$work/config"
conf="$XDG_CONFIG_HOME/kosh/kosh.conf"
mkdir -p "$XDG_CONFIG_HOME/kosh"

do_quiet() {
  "$BIN" "$@" 2>&1 | grep -v '^ '
}

echo "== an invalid KOSHCONF gives the reason:"
for blob in '%%%' AQ== gQA= BQEAAQEA gICAgBA= gICAgJAA; do
  KOSHCONF=$blob do_quiet -c :
  do_quiet -c "koshconf load $blob"
done
echo "== an invalid KOSHCONF does not stop startup:"
KOSHCONF=%%% "$BIN" -c 'koshconf get mood' 2>/dev/null
echo "rc=$?"
echo "== a Bash name points at its koshconf name:"
do_quiet -c 'koshconf get pipefail; koshconf set extglob on'
echo "== enumeration values read naturally:"
do_quiet -c 'koshconf set editor.base_mode ed
koshconf set completion.add_space_after_completed_word true'

echo "== --persist skips a byte order mark:"
printf '\357\273\277bash.interpreter.glob_includes_dotfiles=off\n' >"$conf"
"$BIN" -c 'koshconf set bash.interpreter.glob_includes_dotfiles on --persist'
od -An -c "$conf" | tr -s ' ' | sed 's/ *$//' | grep -v '^$'
echo "== --persist keeps CRLF line ends:"
printf 'mood=bash\r\neditor.auto_close_brackets_and_quotes=off\r\n' >"$conf"
"$BIN" -c 'koshconf set editor.auto_close_brackets_and_quotes on --persist
koshconf set editor.history.max_entries 9 --persist'
od -An -c "$conf" | tr -s ' ' | sed 's/ *$//' | grep -v '^$'
echo "== --persist ends an unterminated last line with the CRLF of the file:"
printf 'mood=bash\r\neditor.auto_close_brackets_and_quotes=off' >"$conf"
"$BIN" -c 'koshconf set editor.history.max_entries 9 --persist'
od -An -c "$conf" | tr -s ' ' | sed 's/ *$//' | grep -v '^$'
echo "== --persist of the kosh mood drops the lines the mood fixes:"
printf '%s\n' mood=bash editor.history.max_entries=9 \
  posix.interpreter.unset_variable_is_error=off \
  posix.interpreter.pipeline_fails_on_any_stage=off \
  bash.interpreter.glob_no_match_is_error=off \
  bash.interpreter.glob_no_match_expands_to_nothing=on \
  interpreter.arithmetic_uses_big_numbers=off >"$conf"
"$BIN" -c 'koshconf set mood kosh --persist'
cat "$conf"
"$BIN" -c ':' 2>&1
echo "startup-rc=$?"
echo "== --persist fills a commented placeholder in place:"
printf '# Store history here.\n# editor.history.file_path=\nmood=bash\n' >"$conf"
"$BIN" -c 'koshconf set editor.history.file_path /tmp/h --persist'
cat "$conf"
echo "== a missing configuration base reports the exact environment state:"
env -u HOME -u XDG_CONFIG_HOME "$BIN" --mood bash -c 'koshconf create kosh' 2>&1 | head -n 1
env -u HOME XDG_CONFIG_HOME=relative "$BIN" --mood bash -c 'koshconf create kosh' 2>&1 | head -n 1
env -u HOME XDG_CONFIG_HOME= "$BIN" --mood bash -c 'koshconf create kosh' 2>&1 | head -n 1
