set -e

"$BIN" --debug-highlight-at \
    'if true; then :; elif false; then :; else :; fi; while true; do :; done; for q in 1; do :; done' |
    grep -E ' :role keyword\)$'

"$BIN" --debug-highlight-at \
    'case x in y) :;; esac; [[ -n x ]]; function f { :; }; time -p -R echo hi' |
    grep -E ' :role (keyword|function-name|flag)\)$'

"$BIN" --debug-highlight-at 'then; done; fi; esac; do echo a; in foo' |
    grep -E ' :role invalid-syntax\)$'

"$BIN" --debug-highlight-at \
    'echo "green" - -x -- --long=value --color="$PATH" *.kosh ./kosh-highlight-missing-path-xyz; kosh-highlight-missing-command-xyz' |
    grep -E ' :role (string|flag|glob|invalid-path|unknown-command)\)$'

"$BIN" --debug-highlight-at 'if true
then echo "$MISSING"
fi' | grep -E ' :role (keyword|unset-variable)\)$'

"$BIN" --debug-highlight-at 'for q
do :
done
for r
in a
do :
done' | grep -E ' :role (keyword|variable)\)$'

koshkit_result=$("$BIN" --debug-highlight-at \
    'koshkit ls -l; koshkit koshkit-missing-utility value; koshkit --help; koshkit "cat"; koshkit '\''expr'\''; koshkit koshkit\-missing-escaped; koshkit c\at; koshkit basename; koshkit $'\''c\x61t'\''; koshkit $"cat"; koshkit "overlong-utility"; koshkit "cat')
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "ls" :role resolved-command)'
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "koshkit-missing-utility" :role unknown-command)'
printf '%s\n' "$koshkit_result" | grep -Fx '(span "--help" :role flag)'
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "\"cat\"" :role resolved-command)'
printf '%s\n' "$koshkit_result" |
    grep -Fx "(span \"'expr'\" :role resolved-command)"
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "koshkit\\-missing-escaped" :role unknown-command)'
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "c\\at" :role resolved-command)'
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "basename" :role resolved-command)'
printf '%s\n' "$koshkit_result" |
    grep -Fx "(span \"\$'c\\\\x61t'\" :role resolved-command)"
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "$\"cat\"" :role resolved-command)'
printf '%s\n' "$koshkit_result" |
    grep -Fx '(span "\"overlong-utility\"" :role unknown-command)'
printf '%s\n' "$koshkit_result" | grep -Fx '(span "\"cat" :role string)'
echo "koshkit static operands are classified"

# Inside [[ ]], && || and ( join tests, so the words after them are no
# commands, and the command after ]] && is one.
"$BIN" --debug-highlight-at '[[ a == b && c ]] && ls; [[ -f x || ( zzz == y ) ]]' |
    grep -E ' :role (resolved-command|unknown-command)\)$'
