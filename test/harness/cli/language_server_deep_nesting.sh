directory=$(mktemp -d) || exit 1
trap '[ -n "$directory" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT

# A message nested past the parser's depth limit gets a parse error reply,
# and a document with thousands of nested command substitutions is highlighted
# and hovered, instead of either overflowing the server's stack.

frame()
{
  body=$1
  printf 'Content-Length: %s\r\n\r\n%s' "${#body}" "$body"
}

brackets=$("$BIN" -c 'printf "%200000s" "" | koshkit tr " " "["')
frame "$brackets" > "$directory/brackets"
"$BIN" --as-language-server < "$directory/brackets" > "$directory/brackets.out"
printf 'deep-message-status=%s\n' "$?"
case $(cat "$directory/brackets.out") in
*'"code":-32700'*) printf 'deep-message-reply=parse-error\n' ;;
*) printf 'deep-message-reply=missing\n' ;;
esac

nested=$("$BIN" -c 'printf "echo "; for ((i = 0; i < 4000; i++)); do printf "\$("; done; printf x; for ((i = 0; i < 4000; i++)); do printf ")"; done')
{
  frame '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"capabilities":{}}}'
  frame '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///tmp/nested.sh","languageId":"shellscript","version":1,"text":"'"$nested"'\n"}}}'
  frame '{"jsonrpc":"2.0","id":2,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///tmp/nested.sh"}}}'
  frame '{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///tmp/nested.sh"},"position":{"line":0,"character":6000}}}'
  frame '{"jsonrpc":"2.0","id":4,"method":"shutdown","params":null}'
  frame '{"jsonrpc":"2.0","method":"exit"}'
} > "$directory/nested"
"$BIN" --as-language-server < "$directory/nested" > "$directory/nested.out"
printf 'nested-document-status=%s\n' "$?"
case $(cat "$directory/nested.out") in
*'"id":4,"result":null'*) printf 'nested-document-shutdown=answered\n' ;;
*) printf 'nested-document-shutdown=missing\n' ;;
esac
