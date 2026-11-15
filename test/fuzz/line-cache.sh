#!/bin/sh
# Build assert-enabled harnesses with make -C src MODE=fuzz fuzz first.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
dir=$(mktemp -d) || exit 1
trap 'rm -rf "$dir"' EXIT

printf 'a\nb' > "$dir/config-a"
printf '\n??' > "$dir/config-b"
printf '\133\050\050\050\050\150\133\377\042\000\010\000\010\015\042' > "$dir/source-a"
printf '\200\011\043\050\012\065\067\021\042\321\074\166\012\012\015' > "$dir/source-b"

# Each pair runs in one process; isolated input replay misses cache reuse.
for harness in Koshconf Source; do
  case "$harness" in
    Koshconf) prefix=config ;;
    Source) prefix=source ;;
  esac
  if "$root/kosh-fuzz-$harness" -runs=1 \
      "$dir/$prefix-a" "$dir/$prefix-b" > "$dir/log" 2>&1; then
    printf '%s ordered line-cache regression: ok\n' "$harness"
  else
    cat "$dir/log" >&2
    exit 1
  fi
done
