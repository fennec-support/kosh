set -e

# A closing brace inside quotes does not end a parameter expansion, and a
# case pattern after | or on its own line is not a command.

result=$("$BIN" --debug-highlight-at 'echo ${x:-"}"} $HOME')
printf '%s\n' "$result" | grep -Fx '(span "${x:-\"}\"}" :role variable)'
printf '%s\n' "$result" | grep -Fx '(span "$HOME" :role variable)'

result=$("$BIN" --debug-highlight-at 'case $x in a|b) echo hi ;; esac')
if printf '%s\n' "$result" | grep -F "unknown-command"; then exit 1; fi
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'case $x in
a|b) echo hi ;; esac')
if printf '%s\n' "$result" | grep -F "unknown-command"; then exit 1; fi
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'case $x in
  a) echo ;;
  b|c) echo ;;
esac
function f { :; }')
if printf '%s\n' "$result" | grep -F "unknown-command"; then exit 1; fi
printf '%s\n' "$result" | grep -Fx '(span "esac" :role keyword)'
printf '%s\n' "$result" | grep -Fx '(span "function" :role keyword)'
