# A file whose name holds a C0 control, DEL, or a C1 control completes in the
# $'...' form with escapes, so the line never holds those bytes raw. Inside an
# open quote each control run closes the quote, is written as $'...', and
# reopens it. A hermetic temp directory keeps the candidates stable.
dir=$(mktemp -d)
trap '[ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
: > "$dir/$(printf 'osc\033]0;title\007x')"
: > "$dir/$(printf 'csi\302\233y')"
: > "$dir/$(printf "mix'q\\\\b\177z")"
cd "$dir"
echo "== an escape and a bell complete as escapes:"
"$BIN" --debug-complete-at 'cat os' </dev/null
echo "== a C1 control completes as hex escapes:"
"$BIN" --debug-complete-at 'cat cs' </dev/null
echo "== a quote, a backslash, and DEL are escaped together:"
"$BIN" --debug-complete-at 'cat mi' </dev/null
echo "== an open single quote splices each control run:"
"$BIN" --debug-complete-at "cat 'os" </dev/null
echo "== an open double quote splices each control run:"
"$BIN" --debug-complete-at 'cat "cs' </dev/null
echo "== the completed word names the file:"
word=$("$BIN" --debug-complete-at 'cat os' </dev/null)
"$BIN" -c "test -e $word && echo found" 2>&1
