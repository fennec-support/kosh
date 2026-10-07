unset KOSH_FLAGS
directory=
trap 'test -n "$directory" && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT
# A function runs in the mood it was defined in. A function defined in bash mood
# expands an unset variable to empty even after the session switches to the
# strict default, and a function defined in the default mood stays strict even
# when it is called from bash mood.
echo "== bash-defined function stays lax after switching to kosh:"
"$BIN" -c 'set -M bash; f() { echo "[${UNSET}]"; }; set -M kosh; f; echo ok'
echo "== kosh-defined function stays strict when called from bash mood:"
"$BIN" -c 'f() { echo "[${UNSET}]"; }; set -M bash; f' 2>&1 | grep -o "is not set" | head -1
echo "== sourced function keeps its source mood:"
directory=$(mktemp -d)
printf '%s\n' 'f() { printf "inside=%s unset=[%s]\n" "$(set -M)" "$NEVER_SET"; }' > "$directory/functions"
"$BIN" -M bash -c ". '$directory/functions'; set -M kosh; f; printf 'outside=%s\\n' \"\$(set -M)\""
echo "== strictness changes from a bash function reach a bash caller:"
"$BIN" -c '
set -M bash
f() { set +u; set +o pipefail; shopt -u failglob; }
g() { f; [[ -o nounset ]] || echo nounset=off
  [[ -o pipefail ]] || echo pipefail=off
  shopt -q failglob || echo failglob=off; }
set -M kosh
g
'
echo "== a bash function cannot relax the kosh mood:"
"$BIN" -c '
set -M bash
f() { set +u; set +o pipefail; shopt -u failglob; }
set -M kosh
f
[[ -o nounset ]] && echo nounset=on
[[ -o pipefail ]] && echo pipefail=on
shopt -q failglob && echo failglob=on
'
echo "== propagated mood carries its strictness:"
"$BIN" -c '# shellcheck disable=unassigned-variable-read
set -M bash
f() { set -M sh; }
set -M kosh
f
printf "mood=%s unset=[%s]\n" "$(set -M)" "$NEVER_SET"
'
test -n "$directory" && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"
directory=
echo "rc-done"
