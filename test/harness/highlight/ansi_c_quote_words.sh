set -e

quote=$(printf "'")

result=$("$BIN" --debug-highlight-at "v=\$${quote}a\\${quote}b${quote}; echo done")
printf '%s\n' "$result" |
    grep -Fx "(span \"\$${quote}a\\\\${quote}b${quote}\" :role string)"
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'

result=$("$BIN" --debug-highlight-at "chars=\$${quote}\\t\\n !\"&\\${quote}();<>|${quote} # a note
echo after")
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'

result=$("$BIN" --debug-highlight-at "v=\$${quote}plain${quote}; echo tail")
printf '%s\n' "$result" |
    grep -Fx "(span \"\$${quote}plain${quote}\" :role string)"
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'v=$"plain $HOME $(echo nested)"')
printf '%s\n' "$result" | grep -Fx '(span "$\"plain " :role string)'
printf '%s\n' "$result" | grep -Fx '(span "$HOME" :role variable)'
printf '%s\n' "$result" | grep -Fx '(span "echo" :role resolved-command)'
printf '%s\n' "$result" | grep -Fx '(span "\"" :role string)'
