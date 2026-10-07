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

echo "== set changes the session, and --persist rewrites one line:"
"$BIN" -c 'koshconf set editor.auto_pair true; koshconf get editor.auto_pair'
line_count=$(grep -c . "$conf")
"$BIN" -c 'koshconf set editor.auto_pair 1 --persist'
grep '^editor.auto_pair=' "$conf"
[ "$(grep -c . "$conf")" = "$line_count" ] && echo "same line count"
echo "== --persist appends a missing option and quotes a padded value:"
printf '# kept\nmood=bash\nmood=sh\n' >"$conf"
"$BIN" -c "koshconf set history.file ' padded ' --persist"
"$BIN" -c 'koshconf set mood kosh --persist'
cat "$conf"
echo "== --persist creates the file and its directory:"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
"$BIN" -c 'koshconf set completion.space_after on --persist'
cat "$conf"
echo "== --persist rejects an option that changes evaluation:"
"$BIN" -c 'koshconf set legacy.exit_on_error on --persist'
echo "rc=$?"

echo "== enumerations and booleans:"
"$BIN" -c 'koshconf set editor.tab_selector plain
koshconf get editor.tab_selector
koshconf set diagnostics.level 2
echo "$-"
koshconf set legacy.exit_on_error on
set -o | grep errexit
koshconf set legacy.vi_editing off
koshconf get legacy.vi_editing'

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
"$BIN" -c 'koshconf load BQEByAEBAQ==; koshconf get editor.auto_pair'
echo "rc=$?"
"$BIN" -c 'koshconf load "not base64"'
echo "rc=$?"
"$BIN" -c 'koshconf load BQ=='
echo "rc=$?"
