# The getconf variable list comes from the koshkit getconf tables. A path
# variable takes a path operand next, and other operands complete as paths.
DIRECTORY=$(mktemp -d) || exit 1
trap '[ -n "$DIRECTORY" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$DIRECTORY"' EXIT
mkdir "$DIRECTORY/subdir"

echo "== a prefix selects system variables through koshkit:"
"$BIN" --debug-complete-at 'koshkit getconf ARG' </dev/null
echo "== a prefix selects path variables through koshkit:"
"$BIN" --debug-complete-at 'koshkit getconf PATH_' </dev/null
echo "== a bare utility completes variables when no program shadows it:"
"$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on' \
    --debug-complete-at 'getconf NAME_M' </dev/null
echo "== the full list is plausible:"
"$BIN" --debug-complete-at 'koshkit getconf ' </dev/null |
  awk 'END { print (NR > 100 ? "many" : "few") }'
echo "== a path variable takes a path operand:"
"$BIN" --debug-complete-at "koshkit getconf PATH_MAX $DIRECTORY/sub" </dev/null |
  sed "s|$DIRECTORY|DIR|"
echo "== the all flag takes a path operand:"
"$BIN" --debug-complete-at "koshkit getconf -a $DIRECTORY/sub" </dev/null |
  sed "s|$DIRECTORY|DIR|"
echo "== a variable operand after a variable offers nothing:"
"$BIN" --debug-complete-at 'koshkit getconf ARG_MAX ARG' </dev/null
