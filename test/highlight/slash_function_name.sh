set -e

tab=$(printf '\t')

result=$("$BIN" --debug-highlight-at 'function ble/util/put { :; }; ble/util/put x')
printf '%s\n' "$result" | grep -E "^ble/util/put${tab}function-name$"
printf '%s\n' "$result" | grep -E "^ble/util/put${tab}resolved-command$"

result=$("$BIN" --debug-highlight-at 'ble/variable#load:x() { :; }; ble/variable#load:x')
printf '%s\n' "$result" | grep -E "^ble/variable#load:x${tab}function-name$"
printf '%s\n' "$result" | grep -E "^ble/variable#load:x${tab}resolved-command$"

existing_path=$BIN
missing_path=$TEST_MKTEMP_DIRECTORY/highlight-missing
missing_name=${missing_path##*/}
result=$("$BIN" --debug-highlight-at "$existing_path; $missing_path")
printf '%s\n' "$result" | grep -Fx "${existing_path}${tab}existing-path" >"$TEST_NULL_DEVICE"
printf '%s\n' "$result" | grep -Fx "${missing_name}${tab}invalid-path" >"$TEST_NULL_DEVICE"
printf 'existing-path\texisting-path\n'
printf 'missing-path\tinvalid-path\n'
