unset KOSH_FLAGS KOSHCONF
# The shell accepts bash's invocation forms -o NAME, +o NAME, -O NAME,
# +O NAME, and +letter. Each named option keeps its command-line value over
# the settings files, and --no-config reads no settings file at all.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
mkdir -p "$work/config/kosh"
export XDG_CONFIG_HOME="$work/config"
printf '%s\n' mood=bash legacy.exit_on_command_failure=on \
  legacy.pipeline_fails_on_any_stage=on legacy.glob_extended_patterns=on \
  legacy.base_editor_mode=vi >"$work/config/kosh/kosh.conf"

do_show() {
  "$BIN" "$@" -c 'printf "%s pipefail=%s extglob=%s editor=%s\n" "$-" \
    "$(shopt -qo pipefail && echo on || echo off)" \
    "$(shopt -q extglob && echo on || echo off)" \
    "$(koshconf get legacy.base_editor_mode)"'
}

echo "== the settings file applies:"
do_show
echo "== +e, +o, and +O keep the options off:"
do_show +e +o pipefail +O extglob +o vi
echo "== -o and -O name options the file leaves alone:"
do_show -o noclobber -O nocasematch
"$BIN" -o noclobber -O nocasematch -c 'shopt -qo noclobber && echo noclobber
shopt -q nocasematch && echo nocasematch'
echo "== the later of a letter and its name wins:"
do_show -e +o errexit
do_show +o errexit -e
do_show +eu -o nounset
echo "== -euo pipefail names pipefail with -o:"
"$BIN" --no-config -euo pipefail -c 'echo "$-"; shopt -qo pipefail && echo pipefail'
echo "== -o posix acts as --posix:"
"$BIN" -o posix -c 'koshconf get mood'
echo "== a read-only shopt name is accepted without effect:"
"$BIN" -O login_shell -c 'shopt -q login_shell || echo not-login'
echo "== an unknown name or letter is a usage error:"
"$BIN" -o nosuch -c 'echo ran'
echo "rc=$?"
"$BIN" +O nosuch -c 'echo ran'
echo "rc=$?"
"$BIN" +r -c 'echo ran' 2>&1 | grep -v '^ ' | sed 's/^[0-9]*:[0-9]*: //'
echo "rc=${PIPESTATUS[0]}"
echo "== the kosh mood refuses to turn off an option it holds:"
"$BIN" --mood kosh +o pipefail -c 'echo ran'
echo "rc=$?"
echo "== a plus word after the script is an operand:"
printf 'echo "operand=$1"\n' >"$work/script.sh"
"$BIN" "$work/script.sh" +e
echo "== --no-config reads no settings file and drops KOSHCONF:"
KOSHCONF=AQEB "$BIN" --no-config -c 'koshconf get mood; echo "$-"
env | grep -c "^KOSHCONF=" || :'
