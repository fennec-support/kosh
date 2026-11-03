set -e

result=$("$BIN" --debug-highlight-at 'echo one
# a note
echo two')
printf '%s\n' "$result" | grep -Fx '(span "# a note" :role comment)'
printf '%s\n' "$result" |
    grep -c -Fx '(span "echo" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'function outer {
  # a note
  echo body
}
outer')
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'
printf '%s\n' "$result" | grep -Fx '(span "outer" :role resolved-command)'
