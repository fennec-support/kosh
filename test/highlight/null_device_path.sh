set -e

null_device=$TEST_NULL_DEVICE
result=$("$BIN" --debug-highlight-at "echo x >$null_device")
tab=$(printf '\t')
printf '%s\n' "$result" | grep -Fx "${null_device}${tab}existing-path" >"$TEST_NULL_DEVICE"
printf 'null-device\texisting-path\n'
