# The koshkit catalog supplies utility names and registered utility flags.
# Bare utility flags are completed when the koshkit option resolves the command.
DIRECTORY=$(mktemp -d) || exit 1
trap '[ -n "$DIRECTORY" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$DIRECTORY"' EXIT
printf '#!/bin/sh\nexit 0\n' > "$DIRECTORY/ls"
chmod +x "$DIRECTORY/ls"

echo "== koshkit utilities by prefix:"
"$BIN" --debug-complete-at 'koshkit m' </dev/null
echo "== ls flags through koshkit:"
"$BIN" --debug-complete-at 'koshkit ls -' </dev/null
echo "== du flags through koshkit:"
"$BIN" --debug-complete-at 'koshkit du -' </dev/null
echo "== timeout flags through koshkit:"
"$BIN" --debug-complete-at 'koshkit timeout -' </dev/null
echo "== nproc flags through koshkit:"
"$BIN" --debug-complete-at 'koshkit nproc -' </dev/null
for utility in evil evildisk evilfiles evilfs evilio evillogs eviliso evilnet evilps \
  evilss goodcore goodfsw goodnode goodstat retry stat sync watch; do
  echo "== $utility flags through koshkit:"
  "$BIN" --debug-complete-at "koshkit $utility -" </dev/null
done
echo "== cat flags through koshkit:"
"$BIN" --debug-complete-at 'koshkit cat --s' </dev/null
echo "== date format directives through koshkit:"
"$BIN" --debug-complete-at 'koshkit date +' </dev/null
echo "== koshkit own flags:"
"$BIN" --debug-complete-at 'koshkit --' </dev/null
echo "== bare utility flags under koshconf set kosh.interpreter.resolve_koshkit_applets_as_commands on:"
"$BIN" -c 'PATH=; koshconf set kosh.interpreter.resolve_koshkit_applets_as_commands on' --debug-complete-at 'ls -' </dev/null
echo "== bare utility names in the default mood:"
"$BIN" -c 'PATH=' --debug-complete-at 'whoa' </dev/null
echo "== bare utility flags in the default mood:"
"$BIN" -c 'PATH=' --debug-complete-at 'ls -A' </dev/null
echo "== bare utility names stay hidden in bash mood:"
"$BIN" -M bash -c 'PATH=' --debug-complete-at 'whoa' </dev/null
echo "== bare utility names appear after koshconf set kosh.interpreter.resolve_koshkit_applets_as_commands on:"
"$BIN" -M bash -c 'PATH=; koshconf set kosh.interpreter.resolve_koshkit_applets_as_commands on' \
  --debug-complete-at 'whoa' </dev/null
echo "== a PATH program keeps its own flags:"
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$DIRECTORY" \
  "$BIN" -c 'koshconf set kosh.interpreter.resolve_koshkit_applets_as_commands on' --debug-complete-at 'ls -A' </dev/null
