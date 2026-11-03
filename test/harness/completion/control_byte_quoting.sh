# A file whose name holds a C0 control, DEL, or a C1 control completes in the
# $'...' form with escapes, so the line never holds those bytes raw. Inside an
# open quote each control run closes the quote, is written as $'...', and
# reopens it. A noquote file name and a spec word keep their other bytes raw
# but splice each control run the same way, while compgen lists entries raw as
# bash does. A hermetic temp directory keeps the candidates stable.
dir=$(mktemp -d)
trap '[ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
: > "$dir/$(printf 'osc\033]0;title\007x')"
: > "$dir/$(printf 'csi\302\233y')"
: > "$dir/$(printf "mix'q\\\\b\177z")"
cd "$dir"
echo "== an escape and a bell complete as escapes:"
"$BIN" --debug-complete-at 'cat os' </dev/null
echo "== a C1 control completes as octal escapes:"
"$BIN" --debug-complete-at 'cat cs' </dev/null
echo "== a quote, a backslash, and DEL are escaped together:"
"$BIN" --debug-complete-at 'cat mi' </dev/null
echo "== an open single quote splices each control run:"
"$BIN" --debug-complete-at "cat 'os" </dev/null
echo "== an open double quote splices each control run:"
"$BIN" --debug-complete-at 'cat "cs' </dev/null
echo "== noquote keeps blanks raw and still splices each control run:"
: > "$dir/$(printf 'nq a\033b')"
"$BIN" --debug-complete-at 'cp nq' -c "_f(){ :; }; complete -o default -o noquote -F _f cp" </dev/null
echo "== a spec word splices each control run:"
"$BIN" --debug-complete-at 'cp w' -c "complete -W \"\$(printf 'w\\033x')\" cp" </dev/null
echo "== compgen lists the entries raw:"
"$BIN" -c 'compgen -f os' | cat -v
echo "== the completed word is the file:"
word=$("$BIN" --debug-complete-at 'cat os' </dev/null |
  sed -e 's/^(candidate "//' -e 's/")$//' -e 's/\\\(["\\]\)/\1/g')
"$BIN" -c "test -e $word && echo found" 2>&1
