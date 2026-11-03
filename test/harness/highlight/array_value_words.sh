set -e

result=$("$BIN" --debug-highlight-at 'keywords1=(if then case esac function); greet() { :; }; greet')
printf '%s\n' "$result" | grep -Fx '(span "keywords1" :role assignment-name)'
printf '%s\n' "$result" | grep -Fx '(span "greet" :role function-name)'
printf '%s\n' "$result" | grep -Fx '(span "greet" :role resolved-command)'
if printf '%s\n' "$result" | grep -qE ' :role keyword\)$'; then
  printf 'array elements were read as keywords\n'
fi

result=$("$BIN" --debug-highlight-at 'declare -a table=(case esac); echo ok')
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'
if printf '%s\n' "$result" | grep -qE ' :role keyword\)$'; then
  printf 'a declare operand was read as keywords\n'
fi

result=$("$BIN" --debug-highlight-at 'arr=("$HOME/bin"); if true; then :; fi')
printf '%s\n' "$result" | grep -Fx '(span "$HOME" :role variable)'
printf '%s\n' "$result" | grep -Fx '(span "if" :role keyword)'
printf '%s\n' "$result" | grep -Fx '(span "fi" :role keyword)'

result=$("$BIN" --debug-highlight-at 'arr=(
then
if
); echo ok')
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'
if printf '%s\n' "$result" | grep -qE ' :role (keyword|invalid-syntax)\)$'; then
  printf 'a multi-line array element was read as a keyword\n'
fi
