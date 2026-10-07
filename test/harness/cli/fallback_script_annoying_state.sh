unset KOSH_FLAGS KOSH_ANALYSIS
# A script without a shebang runs in a fallback context that inherits the
# analysis state of the shell, so the annoying diagnostics switch the parent
# turned off stays off inside it.
directory=$(mktemp -d)
trap '[ -n "$directory" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT

printf '%s\n' \
  '[ "$(koshconf get diagnostics.show_annoying_tier)" = on ] && echo script-annoying-on || echo script-annoying-off' \
  > "$directory/probe"
"$BIN_DIR/invoke-koshkit" chmod +x "$directory/probe"

echo "default: $("$BIN" -c "$directory/probe" 2>&1)"
echo "runtime off: $("$BIN" -c "koshconf set diagnostics.show_annoying_tier off; $directory/probe" 2>&1)"
echo "flag off: $("$BIN" --no-annoying-diagnostics -c "$directory/probe" 2>&1)"
echo "restored: $("$BIN" -c "koshconf set diagnostics.show_annoying_tier off; koshconf set diagnostics.show_annoying_tier on; $directory/probe" 2>&1)"
