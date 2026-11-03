set -e

result=$("$BIN" --debug-highlight-at 'function ble/highlight/layer:{selection}/getg { :; }; ble/highlight/layer:{selection}/getg')
printf '%s\n' "$result" | grep -Fx '(span "ble/highlight/layer:{selection}/getg" :role function-name)'
printf '%s\n' "$result" | grep -Fx '(span "ble/highlight/layer:{selection}/getg" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'ble/prompt/unit:{section}/get() { :; }; ble/prompt/unit:{section}/get')
printf '%s\n' "$result" | grep -Fx '(span "ble/prompt/unit:{section}/get" :role function-name)'
printf '%s\n' "$result" | grep -Fx '(span "ble/prompt/unit:{section}/get" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'function bad{1..3} { :; }')
printf '%s\n' "$result" | grep -Fx '(span "bad{1..3}" :role invalid-syntax)'

result=$("$BIN" --debug-highlight-at 'function bad{unclosed { :; }')
printf '%s\n' "$result" | grep -Fx '(span "bad{unclosed" :role invalid-syntax)'

result=$("$BIN" --debug-highlight-at 'g; g() { :; }; g')
printf '%s\n' "$result" | grep -Fx '(span "g" :role unknown-command)'
printf '%s\n' "$result" | grep -Fx '(span "g" :role function-name)'
printf '%s\n' "$result" | grep -Fx '(span "g" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'function ble/util/put { :; }; ble/util/put x')
printf '%s\n' "$result" | grep -Fx '(span "ble/util/put" :role function-name)'
printf '%s\n' "$result" | grep -Fx '(span "ble/util/put" :role resolved-command)'

result=$("$BIN" --debug-highlight-at 'ble/variable#load:x() { :; }; ble/variable#load:x')
printf '%s\n' "$result" | grep -Fx '(span "ble/variable#load:x" :role function-name)'
printf '%s\n' "$result" | grep -Fx '(span "ble/variable#load:x" :role resolved-command)'
