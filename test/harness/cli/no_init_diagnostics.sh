unset KOSH_FLAGS
# -WWW reports an unset variable read in the rc. --no-init-diagnostics silences
# the startup stage while keeping -WWW for the session.
home=$(mktemp -d)
trap '[ -n "$home" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$home"' EXIT
printf 'echo "rc[${UNSET_IN_RC}]"\n' > "$home/.bashrc"
echo "== -WWW warns during init:"
HOME="$home" "$BIN" -WWW -L bash -i </dev/null 2>&1 | grep -c "is not set"
echo "== --no-init-diagnostics silences init:"
HOME="$home" "$BIN" -WWW --no-init-diagnostics -L bash -i </dev/null 2>&1 | grep -c "is not set"
echo "== -WWW stays active for the session:"
"$BIN" -WWW --no-init-diagnostics -c 'echo "[${UNSET_AT_PROMPT}]"' 2>&1 | grep -c "is not set"
printf 'koshconf set diagnostics.analysis off\n[ "$(koshconf get diagnostics.analysis)" = off ] && echo diagnostics-disabled=1\n' \
  > "$home/.bashrc"
echo "== a startup diagnostics change survives suppression:"
HOME="$home" "$BIN" --no-init-diagnostics -L bash -i </dev/null 2>&1 | \
  grep -c '^diagnostics-disabled=1$'
printf 'set -W\n[ "$(koshconf get diagnostics.analysis)" = off ] && echo diagnostics-still-suppressed=1\n' \
  > "$home/.bashrc"
echo "== a startup warning change does not end suppression:"
HOME="$home" "$BIN" --no-init-diagnostics -L bash -i </dev/null 2>&1 | \
  grep -c '^diagnostics-still-suppressed=1$'
rm -f "$home/.bashrc"
printf 'set -WWW\n' > "$home/.profile"
echo "== a startup warning level reaches the session:"
HOME="$home" "$BIN" -l -L sh --no-init-diagnostics -c 'f() { echo "[${UNSET_AT_PROMPT}]"; }; f' 2>&1 | \
  grep -c "^-c:1:14: warning: The variable 'UNSET_AT_PROMPT' is not set"
