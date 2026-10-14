unset KOSH_FLAGS KOSHCONF
# A restricted shell refuses the koshkit builtin and falls back to a bundled
# utility for a bare word only after a startup file enabled it. No other
# bundled utility runs a program or writes a file, and type, command -v, and
# hash do not offer one.
work=$(mktemp -d)
trap 'cd / && [ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
mkdir "$work/empty" "$work/run"
cd "$work/run" || exit 1

do_restricted() {
  env -i ASAN_OPTIONS="${ASAN_OPTIONS-}" PATH="$work/empty" HOME="$work" XDG_CONFIG_HOME="$work/config" \
    "$BIN" -r "$@" 2>&1 | grep -v '^ '
}

for form in 'env sh -c "echo x > escaped.txt"' 'cp escaped.txt copy.txt' \
  'tee tee.txt </dev/null' 'koshkit env sh -c "echo x > escaped.txt"' \
  'koshkit tee tee.txt </dev/null' 'koshkit find . -exec touch found.txt ";"' \
  'echo xargs.txt | koshkit xargs touch' 'koshkit rm --dry-run -- run' \
  'koshkit --list' 'koshkit --help'
do
  echo "== kosh -r: $form"
  do_restricted -c "$form; echo next status=\$?"
done

echo "== the bash mood:"
do_restricted --mood bash -c 'env sh -c "echo x > escaped.txt"; echo "env status=$?"
koshkit tee tee.txt </dev/null; echo "koshkit status=$?"'

echo "== set -r refuses a utility resolved before it:"
env -i ASAN_OPTIONS="${ASAN_OPTIONS-}" PATH="$work/empty" HOME="$work" XDG_CONFIG_HOME="$work/config" \
  "$BIN" -c 'do_run() { basename a/before; }; do_run; set -r; do_run
echo "status=$?"' 2>&1 | grep -v '^ '

echo "== type, command -v, and hash do not offer a bundled utility:"
do_restricted -c 'type env; echo "type status=$?"
command -v env; echo "command status=$?"
hash env; echo "hash status=$?"'

echo "== a startup file enables a utility and disables a builtin:"
printf 'enable basename\nenable -n echo\n' >"$work/startup"
env -i ASAN_OPTIONS="${ASAN_OPTIONS-}" PATH="$work/empty" HOME="$work" XDG_CONFIG_HOME="$work/config" \
  BASH_ENV="$work/startup" "$BIN" -r --mood bash -c 'basename a/b; type -t basename
enable echo; printf "enable status=%s\n" "$?"
enable -n basename; printf "disable status=%s\n" "$?"
koshkit basename a/b; printf "koshkit status=%s\n" "$?"
enable -n type; printf "disable builtin status=%s\n" "$?"
type basename' 2>&1 | grep -v -e '^ ' -e '^note:'

echo "== an unrestricted shell still runs them:"
env -i ASAN_OPTIONS="${ASAN_OPTIONS-}" PATH="$work/empty" HOME="$work" XDG_CONFIG_HOME="$work/config" \
  "$BIN" -c 'command -v env; koshkit basename a/b; echo "status=$?"'

echo "== nothing was written:"
"$BIN_DIR/invoke-koshkit" ls -A "$work/run" | grep -c . || true
