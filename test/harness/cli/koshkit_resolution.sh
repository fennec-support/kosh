# shellcheck disable=unassigned-variable-read
# The koshkit builtin prefix always works. In the default mood a bare coreutil
# name falls back to the koshkit utility when PATH has no binary of that name,
# while the sh mood reports a command not found. The --enable-koshkit flag and
# koshconf set interpreter.resolve_koshkit_applets_as_commands on enable the same fallback in every mood.
# An empty PATH isolates the resolution from the system coreutils.
unset KOSH_FLAGS

dir=$(mktemp -d) || exit 1
trap '[ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
if [ "${OS-}" = Windows_NT ]; then
  printf '@echo PATH seq\r\n' > "$dir/seq.bat"
else
  printf '#!/bin/sh\nprintf "PATH seq\\n"\n' > "$dir/seq"
  "$BIN_DIR/invoke-koshkit" chmod +x "$dir/seq"
fi

echo "=== koshkit prefix always works ==="
"$BIN" -c 'koshkit seq 3'

echo "=== default mood, empty PATH, falls back to koshkit ==="
"$BIN" -c 'PATH=; seq 3'
echo "rc=$?"

echo "=== sh mood, empty PATH, not found ==="
{
    "$BIN" --mood sh -c 'PATH=; seq 3' 2>&1
    printf 'rc=%s\n' "$?"
} | "invoke-normalize-trace" "$BIN"

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on turns bare names on ==="
"$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on; seq 3'

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on passes bare Koshka utility flags ==="
host_line=$("$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on; evil --short' | sed -n 2p)
case $host_line in
  '  Host '?*) echo 'host-shape=matched' ;;
  *) echo 'host-shape=wrong' ;;
esac

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on passes bare POSIX utility flags ==="
"$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on; ls --help' >"$TEST_NULL_DEVICE"

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on leaves unknown names unresolved ==="
"$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on; command -v KOSH_NOT_A_UTILITY; echo "rc=$?"'

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on preserves builtin precedence ==="
"$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on; echo builtin'

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on preserves alias precedence ==="
"$BIN" -c "PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on; alias seq='echo alias'; seq"

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on preserves function precedence ==="
"$BIN" -c 'PATH=; koshconf set interpreter.resolve_koshkit_applets_as_commands on; seq() { echo function; }; seq'

echo "=== --enable-koshkit turns bare names on ==="
"$BIN" --enable-koshkit -c 'PATH=; seq 3'

echo "=== --enable-koshkit prefers a PATH binary ==="
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --enable-koshkit -c 'seq 3'

echo "=== koshconf set interpreter.resolve_koshkit_applets_as_commands on prefers a PATH binary ==="
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" -c 'koshconf set interpreter.resolve_koshkit_applets_as_commands on; seq 3'

echo "=== --enable-koshkit works in the sh mood ==="
"$BIN" --mood sh --enable-koshkit -c 'PATH=; seq 3'

echo "=== command reports the enabled fallback ==="
"$BIN" --enable-koshkit -c 'PATH=; command -v seq; command -V seq'

echo "=== which reports the enabled fallback ==="
"$BIN" --enable-koshkit -c '# shellcheck disable=which-is-nonstandard
PATH=; which seq' 2>&1 |
  "invoke-normalize-trace" "$BIN"

echo "=== a builtin name is not a koshkit utility ==="
"$BIN" -c 'koshkit echo routed via koshkit' 2>&1
echo "rc=$?"

echo "=== the koshkit name does not recurse ==="
"$BIN" -c 'koshkit koshkit' 2>&1
echo "rc=$?"

echo "=== unknown utility errors ==="
"$BIN" -c 'koshkit nope' 2>&1
echo "rc=$?"
