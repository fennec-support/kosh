unset KOSH_FLAGS KOSHCONF
# The user configuration file applies before the session runs, in interactive
# and non-interactive shells. A malformed line is a located warning that never
# stops the shell. KOSHCONF from the environment applies after the file and is
# removed. A command-line flag and -Q win over the file, and the kosh mood
# sources no rc file.
home=$(mktemp -d)
trap '[ -n "$home" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$home"' EXIT
export HOME="$home"
unset XDG_CONFIG_HOME
conf="$home/.config/kosh/kosh.conf"
mkdir -p "$home/.config/kosh"

cat >"$conf" <<'EOF'
# a comment and a blank line

editor.auto_pair=on
koshkit.commands = true
history.size="128"
diagnostics.level=1
no equals sign
unknown.option=on
editor.hints=maybe
legacy.unset_is_error=off
history.file="unterminated
mood='bash' trailing
EOF
echo "== the file applies with a warning per malformed line:"
"$BIN" -c 'for name in editor.auto_pair koshkit.commands history.size diagnostics.level mood; do
  printf "%s=%s\n" "$name" "$(koshconf get "$name")"
done' >"$home/out" 2>&1
status=$?
sed "s|$home|HOME|" "$home/out"
echo "rc=$status"

echo "== a byte order mark is skipped, and raw bytes are escaped in warnings:"
printf '\357\273\277editor.auto_pair=on\r\nhistory.size=-5\r\n' >"$conf"
printf 'history.file=a\377b\nbad\033[1mname=on\n' >>"$conf"
"$BIN" -c 'koshconf get editor.auto_pair; koshconf get history.size' \
  >"$home/out" 2>&1
status=$?
sed "s|$home|HOME|" "$home/out" | od -An -c | grep -c '033'
sed "s|$home|HOME|" "$home/out"
echo "rc=$status"

printf 'mood=bash\neditor.auto_pair=on\nkoshkit.commands=off\ndiagnostics.level=2\n' >"$conf"
echo "== a configured mood selects the session mood:"
"$BIN" -c 'koshconf get mood; echo "${BASH_VERSION:+bash identity}"'
echo "== a command-line flag wins over the file:"
"$BIN" --mood kosh --enable-koshkit -W -c 'koshconf get mood
koshconf get koshkit.commands
koshconf get diagnostics.level'
echo "== -Q skips the configuration files:"
"$BIN" -Q -c 'koshconf get mood; koshconf get editor.auto_pair'
echo "== XDG_CONFIG_HOME replaces the default directory:"
XDG_CONFIG_HOME="$home/elsewhere" "$BIN" -c 'koshconf get mood'

echo "== KOSHCONF applies after the file and leaves the environment:"
KOSHCONF=AQEBBQEA "$BIN" -c 'koshconf get mood; koshconf get editor.auto_pair
env | grep -c "^KOSHCONF="'
echo "== an invalid KOSHCONF is a warning:"
KOSHCONF='%%%' "$BIN" -c 'koshconf get mood'
echo "rc=$?"

echo "== the kosh mood sources no rc file:"
printf 'echo koshrc-ran\n' >"$home/.koshrc"
printf 'mood=kosh\n' >"$conf"
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c koshrc-ran
echo "== a configured bash mood sources the bash rc:"
printf 'echo bashrc-ran\n' >"$home/.bashrc"
printf 'mood=bash\n' >"$conf"
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
echo "== an inherited KOSH_HISTORY_SIZE is the history.size option:"
printf 'mood=kosh\n' >"$conf"
KOSH_HISTORY_SIZE=77 "$BIN" -c 'koshconf get history.size; (koshconf get history.size) & wait "$!"'
