# A name that starts with a tilde completes escaped, so accepting it names the
# file instead of a home directory, and a name inside an open $'...' quote
# escapes its backslash and single quote, so accepting it does not name a
# file holding a backspace instead.
dir=$(mktemp -d) || exit 1
trap '[ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$dir"' EXIT
: > "$dir/~"
: > "$dir/~zq_root"
: > "$dir/zq\\back"
: > "$dir/zq'quote"
cd "$dir"
echo "== an escaped tilde completes the escaped names:"
"$BIN" --debug-complete-at 'cat \~' </dev/null
echo "== an ANSI C quote escapes a backslash:"
"$BIN" --debug-complete-at "cat \$'zq\\\\b" </dev/null
"$BIN" --debug-complete-at "cat \$'zqb" </dev/null
echo "== an ANSI C quote escapes a single quote:"
"$BIN" --debug-complete-at "cat \$'zq'\\''q" </dev/null
"$BIN" --debug-complete-at "cat \$'zqq" </dev/null
echo "== a quote after an escaped dollar sign stays a plain single quote:"
: > "$dir/\$zq'; echo injected; '"
"$BIN" --debug-complete-at "cat \\\$'zq" </dev/null
echo "== the completed words name the files:"
"$BIN" -c "ls \\~ \$'zq\\\\back' \$'zq\\'quote' \\\$'zq'\"'\"'; echo injected; '\"'\"''"
