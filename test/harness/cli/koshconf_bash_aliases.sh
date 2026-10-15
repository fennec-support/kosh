unset KOSH_FLAGS KOSHCONF
# A Bash spelling is a second spelling of one registry entry: posix.legacy.*
# for the set options POSIX specifies and bash.legacy.* for the rest.
work=$(mktemp -d)
trap 'cd / && [ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
mkdir -p "$work/config/kosh"
cd "$work" || exit 1
export HOME="$work" XDG_CONFIG_HOME="$work/config"

echo "== a shopt alias and its canonical name are one option:"
"$BIN" --mood bash -c 'koshconf set bash.legacy.failglob off
koshconf get bash.interpreter.glob_no_match_is_error
koshconf get bash.legacy.failglob
shopt failglob
koshconf set bash.interpreter.glob_no_match_is_error on
koshconf get bash.legacy.failglob
shopt failglob' 2>&1

echo "== a set -o alias reaches set -o, \$- and SHELLOPTS:"
"$BIN" --mood bash -c 'koshconf set posix.legacy.errexit on
koshconf get posix.interpreter.exit_on_command_failure
set -o | grep errexit
case $- in *e*) echo flag-e-set ;; esac
case :$SHELLOPTS: in *:errexit:*) echo shellopts-errexit ;; esac' 2>&1

echo "== the editor mode alias reaches the enumeration:"
"$BIN" -c 'koshconf set posix.legacy.vi vi
koshconf get editor.base_mode
koshconf get posix.legacy.vi' 2>&1

echo "== a kosh.conf written with aliases loads:"
printf 'mood=bash\nbash.legacy.extglob=off\nposix.legacy.noclobber=on\n' \
  >config/kosh/kosh.conf
"$BIN" -c 'shopt extglob; set -o | grep noclobber' 2>&1

echo "== both spellings in one file follow the later-line rule:"
printf 'mood=bash\nbash.interpreter.glob_extended_patterns=off\nbash.legacy.extglob=on\n' \
  >config/kosh/kosh.conf
"$BIN" -c 'shopt extglob' 2>&1

echo "== list shows canonical names only:"
"$BIN" -c 'koshconf list | grep -c "^bash\.legacy\.extglob="' 2>&1
"$BIN" -c 'koshconf list | grep -c "^bash.interpreter.glob_extended_patterns="' 2>&1

echo "== an unknown alias and the retired names are errors:"
"$BIN" -c 'koshconf get bash.legacy.foo' 2>&1 | grep -v '^ '
"$BIN" -c 'koshconf get bash.legacy.errexit' 2>&1 | grep -v '^ '
"$BIN" -c 'koshconf get posix.legacy.extglob' 2>&1 | grep -v '^ '
"$BIN" -c 'koshconf get legacy.glob_extended_''patterns' 2>/dev/null
echo "retired status=$?"
"$BIN" -c 'koshconf get startup.init_''moods' 2>/dev/null
echo "retired status=$?"
