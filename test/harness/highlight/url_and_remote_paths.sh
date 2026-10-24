set -e

BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
directory=$(mktemp -d)
trap 'rm -rf "$directory"' EXIT
cd "$directory"

result=$("$BIN" --debug-highlight-at \
    'echo https://example.com/a user@host:/dir/file ./missing:part')
tab=$(printf '\t')

printf '%s\n' "$result" | grep -E "${tab}(url|invalid-path)$"

ssh_result=$("$BIN" --debug-highlight-at 'ssh root@alpha-box')
printf '%s\n' "$ssh_result" | grep -E "^root@alpha-box${tab}"
echo_result=$("$BIN" --debug-highlight-at 'echo root@alpha-box')
printf '%s\n' "$echo_result" | grep -E "${tab}(url|resolved-command)$"

absolute_result=$("$BIN" --debug-highlight-at \
    "echo $directory/absolute-missing")
printf '%s\n' "$absolute_result" | grep -q "${tab}invalid-path$"

if [ "${OS-}" = Windows_NT ]; then
    drive_result=$("$BIN" --debug-highlight-at 'echo C:/missing')
    printf '%s\n' "$drive_result" | grep -q "${tab}invalid-path$"
fi
