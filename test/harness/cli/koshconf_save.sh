unset KOSH_FLAGS KOSHCONF
# koshconf lists name=value lines without a class column, shows the
# posix.legacy and bash.legacy alias of each option with list --all, refuses
# --all outside list, prints a blob that load and a child kosh adopt, and writes
# the current settings with save --persist.
config=$(mktemp -d)
trap '[ -n "$config" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$config"' EXIT
export XDG_CONFIG_HOME="$config/user"
export KOSH_HISTORY_FILE=/history
conf="$XDG_CONFIG_HOME/kosh/kosh.conf"

echo "== list has no class column:"
"$BIN" -c 'koshconf list' | grep -c -E ' +(interactive|semantic|class)$'
echo "== list -a adds the alias after each canonical line:"
"$BIN" -c 'koshconf list -a' | grep -A1 -E '^(posix\.interpreter\.exit_on_command_failure|bash\.interpreter\.glob_extended_patterns)='
echo "== every alias returns the value of its option:"
"$BIN" -c 'koshconf list -a' >"$config/all.txt"
# An option ignored by Koshka has a legacy spelling as its canonical name, so
# the plain list tells canonical lines from aliases.
"$BIN" -c 'koshconf list' >"$config/canonical.txt"
canonical=
while IFS='=' read -r name value
do
  if grep -q "^$name=" "$config/canonical.txt"; then
    canonical=$value
    continue
  fi

  case $name in
    '#'*) continue ;;
    posix.legacy.hashall | posix.legacy.nolog) ;;
    bash.legacy.* | posix.legacy.*)
      alias_value=$("$BIN" -c "koshconf get $name" 2>&1)
      [ "$alias_value" = "$canonical" ] || echo "mismatch: $name"
      [ "$value" = "$canonical" ] || echo "line mismatch: $name"
      ;;
    *) canonical=$value ;;
  esac
done <"$config/all.txt"
echo "== set-only options appear as posix aliases:"
grep -E '^posix\.legacy\.(hashall|nolog)=' "$config/all.txt"
echo "== the editor mode lists no emacs or vi alias:"
grep -c -E '^(bash|posix)\.legacy\.(emacs|vi)=' "$config/all.txt"
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

"$BIN" --mood bash -c 'koshconf set editor.auto_close_brackets_and_quotes on
koshconf list' >"$config/before.txt"
echo "== save --persist writes only the settings that differ from the defaults:"
"$BIN" --mood bash -c 'koshconf set editor.auto_close_brackets_and_quotes on; koshconf save --persist'
echo "rc=$?"
grep -v '^#' "$conf" | grep .
head -1 "$conf"
echo "== the saved file loads back:"
"$BIN" -c 'koshconf get mood; koshconf get editor.auto_close_brackets_and_quotes'
"$BIN" -c 'koshconf list' | diff - "$config/before.txt" && echo same
echo "== save --persist keeps settings this build does not recognize:"
printf 'future.option=on\n' >> "$conf"
"$BIN" --mood bash -c 'koshconf save --persist' 2>/dev/null
grep -c '^future\.option=on$' "$conf"
grep -v '^future\.option=' "$conf" >"$conf.kept" && mv "$conf.kept" "$conf"
echo "== plain save writes no file:"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
"$BIN" -c 'koshconf save' >/dev/null
[ -e "$conf" ] && echo written || echo absent
