set -e

result=$("$BIN" --debug-highlight-at 'echo $[ count[1] + (2 * size) ] *.c; echo after')
printf '%s\n' "$result" | grep -Fx '(span "count" :role unset-variable)'
printf '%s\n' "$result" | grep -Fx '(span "size" :role unset-variable)'
printf '%s\n' "$result" | grep -Fx '(span "[" :role operator)'
printf '%s\n' "$result" | grep -Fx '(span "*" :role operator)'
printf '%s\n' "$result" | grep -Fx '(span "*.c" :role glob)'
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'
if printf '%s\n' "$result" | grep -F ' :role glob)' | grep -Fv '*.c'; then
  exit 1
fi

result=$("$BIN" --debug-highlight-at 'echo $(( 2 * size )) ${list[1]}')
printf '%s\n' "$result" | grep -Fx '(span "${list[1]}" :role variable)'
if printf '%s\n' "$result" | grep -F ' :role glob)'; then
  exit 1
fi
