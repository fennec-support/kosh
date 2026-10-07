set -e

tab=$(printf '\t')

result=$("$BIN" --debug-highlight-at 'echo $[ count[1] + (2 * size) ] *.c; echo after')
printf '%s\n' "$result" | grep -Fx "count${tab}unset-variable"
printf '%s\n' "$result" | grep -Fx "size${tab}unset-variable"
printf '%s\n' "$result" | grep -Fx "[${tab}operator"
printf '%s\n' "$result" | grep -Fx "*${tab}operator"
printf '%s\n' "$result" | grep -Fx "*.c${tab}glob"
printf '%s\n' "$result" | grep -Fx "echo${tab}resolved-command"
if printf '%s\n' "$result" | grep -F "${tab}glob" | grep -Fv '*.c'; then
  exit 1
fi

result=$("$BIN" --debug-highlight-at 'echo $(( 2 * size )) ${list[1]}')
printf '%s\n' "$result" | grep -Fx "\${list[1]}${tab}variable"
if printf '%s\n' "$result" | grep -F "${tab}glob"; then
  exit 1
fi
