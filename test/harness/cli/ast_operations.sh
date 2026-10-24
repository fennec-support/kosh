printf 'echo   format\n' | "$BIN" -A --format
printf 'echo lint\n' | "$BIN" -A --lint --no-traces
printf 'echo   combined\n' | "$BIN" -A --lint --format --no-traces
printf 'name=ok; echo "$name" | koshkit cat\n' | "$BIN" -AR -

root=$TEST_TEMP_DIRECTORY/ast-operations
mkdir -p "$root"
trap '[ -n "$root" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$root"' EXIT

printf '# heading\n\ntext\n\n```sh\necho   embedded\n```\n' > "$root/plain.md"
"$BIN" -A --format "$root/plain.md"
"$BIN" -A --lint --no-traces "$root/plain.md"
