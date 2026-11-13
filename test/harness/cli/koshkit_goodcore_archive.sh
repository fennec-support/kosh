#!/bin/sh

work=$TEST_TEMP_DIRECTORY/goodcore-archive
mkdir -p "$work"
core=$work/fake.core
uncompressed=$work/archive.tar
compressed_work=$work/compressed
mkdir -p "$compressed_work"
cp "$BIN" "$core"
executable_directory=$(cd "$work" && pwd -P)/"binary space 'quote"
mkdir -p "$executable_directory"
executable=$executable_directory/"program 'name"
cp "$BIN" "$executable"

report=$($BIN -c 'koshkit --color never goodcore --binary "$1" --output "$2" --no-compress "$3"' \
  goodcore "$executable" "$uncompressed" "$core" 2> /dev/null)
status=$?
printf 'uncompressed-status=%s\n' "$status"
case $report in
  GOODCORE*|*"  Archive:"*|*FIELD*VALUE*) report_shape=wrong ;;
  *Archive*Executable*Files*) report_shape=matched ;;
  *) report_shape=wrong ;;
esac
printf 'report-layout=%s\n' "$report_shape"

contents=$(tar -tf "$uncompressed")
archive_shape=matched
for required_entry in './dump/core' './INFO.txt' './root/' './debug-gdb.sh' \
  './debug-lldb.sh'; do
  case $contents in
    *"$required_entry"*) ;;
    *) archive_shape=wrong ;;
  esac
done
printf 'uncompressed-contents=%s\n' "$archive_shape"

script_modes=$(tar -tvf "$uncompressed" | grep -c '^-rwxr-xr-x .*debug-\(gdb\|lldb\)\.sh$')
printf 'debugger-scripts-executable=%s\n' "$script_modes"

extracted=$work/"extracted space 'quote"
debuggers=$work/debuggers
mkdir -p "$extracted" "$debuggers"
tar -xf "$uncompressed" -C "$extracted"
extracted=$(cd "$extracted" && pwd -P)
for debugger in gdb lldb; do
  printf '#!/bin/sh\nprintf "%%s\\n" "$@"\n' >"$debuggers/$debugger"
  chmod +x "$debuggers/$debugger"
done
PATH="$(cd "$debuggers" && pwd -P):$PATH" \
  "$extracted/debug-gdb.sh" 'argument with spaces' "single'quote" '' \
  >"$work/gdb-arguments"
printf '%s\n' -ex "set sysroot $extracted/root" \
  -ex "set solib-search-path $extracted/root" \
  "$extracted/root/${executable#/}" "$extracted/dump/core" \
  'argument with spaces' "single'quote" '' >"$work/expected-gdb"
cmp "$work/gdb-arguments" "$work/expected-gdb"
printf 'gdb-launcher-arguments=%s\n' "$?"
PATH="$(cd "$debuggers" && pwd -P):$PATH" \
  "$extracted/debug-lldb.sh" 'argument with spaces' "single'quote" '' \
  >"$work/lldb-arguments"
printf '%s\n' -O "platform select host --sysroot '$extracted/root'" \
  --core "$extracted/dump/core" "$extracted/root/${executable#/}" \
  'argument with spaces' "single'quote" '' >"$work/expected-lldb"
cmp "$work/lldb-arguments" "$work/expected-lldb"
printf 'lldb-launcher-arguments=%s\n' "$?"

quiet_status=0
quiet_output=$($BIN -c \
  'koshkit goodcore --quiet --binary "$1" --output "$2" --no-compress "$3"' \
  goodcore "$BIN" "$work/quiet.tar" "$core" 2>&1) || quiet_status=$?
printf 'quiet-status=%s\n' "$quiet_status"
printf 'quiet-output-bytes=%s\n' "${#quiet_output}"

compressed_status=0
if command -v zstd >/dev/null 2>&1; then
  compressed_suffix=.tar.zst
else
  compressed_suffix=.tar.gz
fi
compressed_target=$compressed_work/archive$compressed_suffix
"$BIN" -c \
  'koshkit goodcore --quiet --binary "$1" --output "$2" "$3"' \
  goodcore "$BIN" "$compressed_target" "$core" \
  >/dev/null 2>&1 || compressed_status=$?
printf 'compressed-status=%s\n' "$compressed_status"
if [ -f "$compressed_target" ]; then
  compressed_shape=matched
else
  compressed_shape=wrong
fi
printf 'compressed-layout=%s\n' "$compressed_shape"
