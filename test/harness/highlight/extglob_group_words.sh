set -e

result=$("$BIN" --debug-highlight-at 'echo @(a|b)<(ls) x; ls')
if printf '%s\n' "$result" | grep -qE '^\(span "a" '; then
  printf 'an extglob group was read as a subshell\n'
fi
printf '%s\n' "$result" | grep -Fx '(span "<(" :role operator)'
printf '%s\n' "$result" | grep -c -Fx '(span "ls" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'case x in @(a|b)) ls ;; esac')
if printf '%s\n' "$result" | grep -qE '^\(span "(a|b)" '; then
  printf 'a case pattern group was read as a subshell\n'
fi
printf '%s\n' "$result" | grep -Fx '(span "ls" :role resolved-command)'
