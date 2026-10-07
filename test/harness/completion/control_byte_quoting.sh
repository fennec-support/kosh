# A file whose name holds a C0 control, DEL, or a C1 control completes in the
# $'...' form with escapes, so the line never holds those bytes raw. Inside an
# open quote each control run closes the quote, is written as $'...', and
# reopens it. A noquote file name and a spec word keep their other bytes raw
# but splice each control run the same way, while compgen lists names raw as
# bash does. A hermetic temp directory keeps the candidates stable.
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
echo "== noquote keeps blanks raw and still splices each control run:"
: > "$dir/$(printf 'nq a\033b')"
"$BIN" -c "_f(){ :; }; complete -o default -o noquote -F _f cp" --debug-complete-at 'cp nq' </dev/null
echo "== a spec word splices each control run:"
"$BIN" -c "complete -W \"\$(printf 'w\\033x')\" cp" --debug-complete-at 'cp w' </dev/null
echo "== compgen lists the names raw:"
"$BIN" -c 'compgen -f os' | cat -v
echo "== the completed word names the file:"
word=$("$BIN" --debug-complete-at 'cat os' </dev/null)
"$BIN" -c "test -e $word && echo found" 2>&1
