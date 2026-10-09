unset KOSH_FLAGS KOSHCONF
# A Bash spelling is a second spelling of one registry entry: legacy.posix.*
# for the set options POSIX specifies and legacy.bash.* for the rest.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
mkdir -p "$work/config/kosh"
cd "$work" || exit 1
export HOME="$work" XDG_CONFIG_HOME="$work/config"

echo "== a shopt alias and its canonical name are one option:"
"$BIN" --mood bash -c 'koshconf set legacy.bash.failglob off
koshconf get kosh.glob_no_match_is_error
koshconf get legacy.bash.failglob
shopt failglob
koshconf set kosh.glob_no_match_is_error on
koshconf get legacy.bash.failglob
shopt failglob' 2>&1

echo "== a set -o alias reaches set -o, \$- and SHELLOPTS:"
"$BIN" --mood bash -c 'koshconf set legacy.posix.errexit on
koshconf get kosh.exit_on_command_failure
set -o | grep errexit
case $- in *e*) echo flag-e-set ;; esac
case :$SHELLOPTS: in *:errexit:*) echo shellopts-errexit ;; esac' 2>&1

echo "== the editor mode alias reaches the enumeration:"
"$BIN" -c 'koshconf set legacy.posix.vi vi
koshconf get editor.base_mode
koshconf get legacy.posix.vi' 2>&1

echo "== a kosh.conf written with aliases loads:"
printf 'kosh.mood=bash\nlegacy.bash.extglob=off\nlegacy.posix.noclobber=on\n' \
  >config/kosh/kosh.conf
"$BIN" -c 'shopt extglob; set -o | grep noclobber' 2>&1

echo "== both spellings in one file follow the later-line rule:"
printf 'kosh.mood=bash\nkosh.glob_extended_patterns=off\nlegacy.bash.extglob=on\n' \
  >config/kosh/kosh.conf
"$BIN" -c 'shopt extglob' 2>&1

echo "== list shows canonical names only:"
"$BIN" -c 'koshconf list | grep -c legacy' 2>&1
"$BIN" -c 'koshconf list | grep -c "^kosh.glob_extended_patterns="' 2>&1

echo "== an unknown alias and the retired names are errors:"
"$BIN" -c 'koshconf get legacy.bash.foo' 2>&1 | grep -v '^ '
"$BIN" -c 'koshconf get legacy.bash.errexit' 2>&1 | grep -v '^ '
"$BIN" -c 'koshconf get legacy.posix.extglob' 2>&1 | grep -v '^ '
"$BIN" -c 'koshconf get legacy.glob_extended_''patterns' 2>/dev/null
echo "retired status=$?"
"$BIN" -c 'koshconf get startup.init_''moods' 2>/dev/null
echo "retired status=$?"
