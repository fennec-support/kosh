set -e

tab=$(printf '\t')

result=$("$BIN" --debug-highlight-at 'mapfile -t v <<< "$HOME"
echo after')
printf '%s\n' "$result" | grep -Fx "<<<${tab}operator"
printf '%s\n' "$result" | grep -E "^echo${tab}resolved-command$"
if printf '%s\n' "$result" | grep -qE "${tab}heredoc(-delimiter)?$"; then
  printf 'a here-string opened a here-document\n'
fi

result=$("$BIN" --debug-highlight-at '((w<<=1, w>2 && (w=2)))
echo after')
printf '%s\n' "$result" | grep -E "^echo${tab}resolved-command$"
if printf '%s\n' "$result" | grep -qE "${tab}heredoc(-delimiter)?$"; then
  printf 'an arithmetic shift opened a here-document\n'
fi

result=$("$BIN" --debug-highlight-at 'cat <<EOF
body
EOF
echo after')
printf '%s\n' "$result" | grep -Fx "EOF${tab}heredoc-delimiter"
printf '%s\n' "$result" | grep -E "${tab}heredoc$"
printf '%s\n' "$result" | grep -E "^echo${tab}resolved-command$"

crlf_source=$(printf 'cat <<_ACEOF arg\r\nbody\r\n_ACEOF\r\necho after')
result=$("$BIN" --debug-highlight-at "$crlf_source")
printf '%s\n' "$result" | grep -c "^_ACEOF${tab}heredoc-delimiter$"
printf '%s\n' "$result" | grep -E "^echo${tab}resolved-command$"

"$BIN" --debug-highlight-at 'cat <<EOF
body
EOF
cat <<-"DONE"
	body
	DONE' | grep -E "${tab}(heredoc|heredoc-delimiter)$"

"$BIN" --debug-highlight-at 'cat <<\_ACEOF
body
_ACEOF
echo tail' | grep -E "${tab}(heredoc|heredoc-delimiter|resolved-command)$"

"$BIN" --debug-highlight-at 'cat <<"END MARK"
END
END MARK
echo tail' | grep -E "${tab}(heredoc|heredoc-delimiter|resolved-command)$"

crlf_source=$(printf 'cat <<EOF\r\nx\r\nEOF\r\nls\r\necho tail')
"$BIN" --debug-highlight-at "$crlf_source" |
  grep -E "${tab}(heredoc|heredoc-delimiter|resolved-command)$"

"$BIN" --debug-highlight-at "cat <<\$'a b'
x
a b
echo tail" | grep -E "${tab}(heredoc|heredoc-delimiter|resolved-command)$"

"$BIN" --debug-highlight-at "cat <<\$'a\\'b' ; echo X
body
a'b
echo tail" | grep -E "${tab}(heredoc|heredoc-delimiter|resolved-command)$"
