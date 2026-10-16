# The koshkit catalog supplies utilities and registered utility flags.
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
echo "== every help flag of a listed utility is completed:"
missing_count=0
for utility in evil evildisk evilfiles evilfs evilio evillogs eviliso evilnet evilps \
  evilss goodcore goodfsw goodnode goodstat retry stat sync watch; do
  offered=" $("$BIN" --debug-complete-at "koshkit $utility -" </dev/null |
    sed 's/=$//' | tr '\n' ' ') "
  flags=$("$BIN" -c "koshkit $utility --help" 2>&1 |
    sed -n '/^OPTIONS$/,/^$/p' |
    sed -n -E '/^(  -|      --)/p' |
    sed -E 's/^(.{0,38}[^ ]) {2,}.*/\1/; s/\[?=.*$//; s/^ +//; s/, /\n/g')
  [ -n "$flags" ] || echo "$utility: no help flags"
  for flag in $flags; do
    case $offered in
      *" $flag "*) ;;
      *)
        echo "$utility: missing $flag"
        missing_count=$((missing_count + 1))
        ;;
    esac
  done
done
[ "$missing_count" -eq 0 ] && echo "ok: koshkit flags complete"
echo "== cat flags through koshkit:"
"$BIN" --debug-complete-at 'koshkit cat --s' </dev/null
echo "== date format directives through koshkit:"
"$BIN" --debug-complete-at 'koshkit date +' </dev/null
echo "== koshkit own flags:"
"$BIN" --debug-complete-at 'koshkit --' </dev/null
echo "== bare utility flags under koshconf set interpreter.resolve_koshkit_applets_as_commands on:"
"$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on' --debug-complete-at 'ls -' </dev/null
echo "== bare utility names in the default mood:"
"$BIN" -c 'PATH=' --debug-complete-at 'whoa' </dev/null
echo "== bare utility flags in the default mood:"
"$BIN" -c 'PATH=' --debug-complete-at 'ls -A' </dev/null
echo "== bare utility names stay hidden in bash mood:"
"$BIN" -M bash -c 'PATH=' --debug-complete-at 'whoa' </dev/null
echo "== bare utility names appear after koshconf set interpreter.resolve_koshkit_applets_as_commands on:"
"$BIN" -M bash -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on' \
  --debug-complete-at 'whoa' </dev/null
echo "== a PATH program keeps its own flags:"
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$DIRECTORY" \
  "$BIN" -c 'koshconf set interpreter.resolve_koshkit_applets_as_commands on' --debug-complete-at 'ls -A' </dev/null
