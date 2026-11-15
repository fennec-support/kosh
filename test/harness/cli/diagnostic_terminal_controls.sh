#!/bin/sh

unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
dir=$(mktemp -d) || exit 1
start=$PWD
trap 'cd "$start" && rm -rf "$dir"' EXIT
cd "$dir" || exit 1

# macOS filenames require valid UTF-8; raw invalid bytes use runtime operands.
name=$(printf 't\033]0;PWNED\007\033[44m\r\n\177\302\233X.kosh')
printf 'echo (\n' > "$name" || exit 1

run_case() {
  label=$1
  shift
  "$@" > out 2> err
  status=$?
  control_count=$(LC_ALL=C tr -d '\012\040-\176' < err | wc -c | tr -d ' ')
  echo "$label: status=$status controls=$control_count"
  grep -F '\E' err > /dev/null
  echo "escaped=$?"
  test ! -s out
  echo "empty stdout=$?"
}

run_case 'lint filename' "$BIN" --lint "$name"
run_case 'syntax filename' "$BIN" -n "$name"
run_case 'runtime command' "$BIN" --no-diagnostics -c '"$1"' name "$name"
run_case 'runtime operand' "$BIN" --no-diagnostics -c 'cd "$1"' name "$name"
run_case 'missing input' "$BIN" --lint "missing-$name"

# Source excerpts also escape controls without moving the diagnostic caret.
printf 'echo ( # \033]0;PWNED\007\033[44m\r\177\302\233\233\n' > source.kosh
run_case 'source excerpt' "$BIN" --lint source.kosh
invalid_name=$(printf 't\033[44m\233X')
run_case 'invalid command byte' "$BIN" --no-diagnostics -c '"$1"' name "$invalid_name"

printf '\t\033[2Jecho (\n' > caret.kosh
run_case 'caret after controls' "$BIN" -n caret.kosh
awk '/\|  / { print }' err
