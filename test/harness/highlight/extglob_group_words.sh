set -e

tab=$(printf '\t')

result=$("$BIN" --debug-highlight-at 'echo @(a|b)<(ls) x; ls')
if printf '%s\n' "$result" | grep -qE "^a${tab}"; then
  printf 'an extglob group was read as a subshell\n'
fi
printf '%s\n' "$result" | grep -Fx "<(${tab}operator"
printf '%s\n' "$result" | grep -c "^ls${tab}resolved-command$"

result=$("$BIN" --debug-highlight-at 'case x in @(a|b)) ls ;; esac')
if printf '%s\n' "$result" | grep -qE "^(a|b)${tab}"; then
  printf 'a case pattern group was read as a subshell\n'
fi
printf '%s\n' "$result" | grep -E "^ls${tab}resolved-command$"
