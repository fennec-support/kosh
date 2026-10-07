set -e

printf '== groups\n'
"$BIN" --debug-brackets-at 'f() { (a [b]); }'

printf '== literal brackets\n'
"$BIN" --debug-brackets-at 'echo ( ")" '"')'"' \) ) # )'

printf '== substitutions\n'
"$BIN" --debug-brackets-at 'echo "$(echo ")")" ${a[1]}'

printf '== doubled forms\n'
"$BIN" --debug-brackets-at '[[ -n x ]] && ((y)) && $((1+(2)))'

printf '== heredoc body\n'
"$BIN" --debug-brackets-at 'cat <<E (
)
E
)'

printf '== case patterns\n'
"$BIN" --debug-brackets-at '(case x in a) :;; (b) :;; esac)'

printf '== unmatched\n'
"$BIN" --debug-brackets-at 'echo ( ] } [ x'
printf 'status=%s\n' "$?"
