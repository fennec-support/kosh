unset KOSH_FLAGS KOSH_ANALYSIS
# A script without a shebang runs in a fallback context that inherits the
# analysis state of the shell, so the annoying diagnostics switch the parent
# turned off stays off inside it.
directory=$(mktemp -d)
trap '[ -n "$directory" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT

printf '%s\n' \
  '[[ -o annoying-diagnostics ]] && echo script-annoying-on || echo script-annoying-off' \
  > "$directory/probe"
"$BIN_DIR/invoke-koshkit" chmod +x "$directory/probe"

echo "default: $("$BIN" -c "$directory/probe" 2>&1)"
echo "runtime off: $("$BIN" -c "set +o annoying-diagnostics; $directory/probe" 2>&1)"
echo "flag off: $("$BIN" --no-annoying-diagnostics -c "$directory/probe" 2>&1)"
echo "restored: $("$BIN" -c "set +o annoying-diagnostics; set -o annoying-diagnostics; $directory/probe" 2>&1)"
