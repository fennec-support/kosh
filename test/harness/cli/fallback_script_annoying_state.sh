unset KOSH_FLAGS KOSH_ANALYSIS
# A script without a shebang runs in a fallback context that inherits the
# analysis state of the shell, so the annoying diagnostics switch the parent
# turned off stays off inside it.
directory=$(mktemp -d)
trap '[ -n "$directory" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT

printf '%s\n' \
  '[ "$(koshconf get diagnostics.annoying)" = on ] && echo script-annoying-on || echo script-annoying-off' \
  > "$directory/probe"
"$BIN_DIR/invoke-koshkit" chmod +x "$directory/probe"

echo "default: $("$BIN" -c "$directory/probe" 2>&1)"
echo "runtime off: $("$BIN" -c "koshconf set diagnostics.annoying off; $directory/probe" 2>&1)"
echo "flag off: $("$BIN" --no-annoying-diagnostics -c "$directory/probe" 2>&1)"
echo "restored: $("$BIN" -c "koshconf set diagnostics.annoying off; koshconf set diagnostics.annoying on; $directory/probe" 2>&1)"
