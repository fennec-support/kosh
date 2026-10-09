unset KOSH_FLAGS KOSHCONF
# koshconf documents save and $KOSHCONF in its help, lists name=value lines
# without a class column, shows the legacy.posix and legacy.bash alias of
# each option with list --all, refuses --all outside list, prints a blob that
# load and a child kosh adopt, writes the current settings with save
# --persist, and keeps save --persist out of a restricted shell.
config=$(mktemp -d)
trap '[ -n "$config" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$config"' EXIT
export XDG_CONFIG_HOME="$config/user"
export KOSH_HISTORY_FILE=/history
conf="$XDG_CONFIG_HOME/kosh/kosh.conf"

echo "== the help documents the forms and \$KOSHCONF:"
"$BIN" -c 'koshconf --help'
echo "== list has no class column:"
"$BIN" -c 'koshconf list' | grep -c -E ' +(interactive|semantic|class)$'
"$BIN" -c 'koshconf list' | head -4
echo "== list -a adds the alias after each canonical line:"
"$BIN" -c 'koshconf list -a' | grep -A1 -E '^interpreter.(exit_on_command_failure|glob_extended_patterns)='
"$BIN" -c 'koshconf list --all' | grep -c '^legacy\.'
echo "== every alias reads the value of its option:"
"$BIN" -c 'koshconf list -a' >"$config/all.txt"
canonical=
while IFS='=' read -r name value
do
  case $name in
    '#'*) continue ;;
    legacy.posix.hashall | legacy.posix.nolog) ;;
    legacy.*)
      alias_value=$("$BIN" -c "koshconf get $name" 2>&1)
      [ "$alias_value" = "$canonical" ] || echo "mismatch: $name"
      [ "$value" = "$canonical" ] || echo "line mismatch: $name"
      ;;
    *) canonical=$value ;;
  esac
done <"$config/all.txt"
echo "== set-only options appear as posix aliases:"
grep -E '^legacy\.posix\.(hashall|nolog)=' "$config/all.txt"
echo "== the editor mode lists no emacs or vi alias:"
grep -c -E '^legacy\.[a-z]+\.(emacs|vi)=' "$config/all.txt"
echo "== --all belongs to list:"
"$BIN" -c 'koshconf set mood bash --all' 2>&1 | grep -E 'error|rc='
"$BIN" -c 'koshconf save --all' 2>&1 | grep error
"$BIN" -c 'koshconf get mood -a' 2>&1 | grep error

echo "== save prints a blob that load accepts:"
blob=$("$BIN" --mood bash -c 'koshconf save')
echo "rc=$?"
[ -n "$blob" ] && echo "blob-printed"
"$BIN" -c "koshconf load '$blob'; koshconf get mood"
echo "== a child adopts KOSHCONF=\$(koshconf save):"
KOSHCONF=$blob "$BIN" -c 'koshconf get mood'
"$BIN" --mood bash -c 'koshconf set editor.auto_close_brackets_and_quotes on; KOSHCONF=$(koshconf save) '"$BIN"' -c "koshconf get mood; koshconf get editor.auto_close_brackets_and_quotes"'
echo "== save refuses an operand:"
"$BIN" -c 'koshconf save now' 2>&1 | grep error

echo "== save --persist writes the current settings:"
"$BIN" --mood bash -c 'koshconf set editor.auto_close_brackets_and_quotes on; koshconf save --persist'
echo "rc=$?"
grep -c . "$conf" | sed 's/^[0-9][0-9]*$/COUNT/'
grep -E '^kosh\.(mood|editor\.auto_close_brackets_and_quotes|editor\.history\.file_path)=' "$conf"
head -1 "$conf"
echo "== the saved file loads back:"
"$BIN" -c 'koshconf get mood; koshconf get editor.auto_close_brackets_and_quotes'
"$BIN" --mood bash -c 'koshconf list' >"$config/before.txt"
"$BIN" --mood bash -c 'koshconf list' | diff - "$config/before.txt" && echo same
echo "== plain save writes no file:"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
"$BIN" -c 'koshconf save' >/dev/null
[ -e "$conf" ] && echo written || echo absent

echo "== a restricted shell prints a blob but refuses save --persist:"
"$BIN" -r -c 'koshconf save | grep -c .; koshconf save --persist; echo "rc=$?"' 2>&1 | grep -v '^ '
[ -e "$conf" ] && echo written || echo absent
