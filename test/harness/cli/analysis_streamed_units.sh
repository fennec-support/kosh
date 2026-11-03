# --lint analyzes a short script as one tree and a script past 256 KiB as
# streamed top-level units after a metadata scan. Both modes report the same
# findings: the long script is the short one with `:` lines after its
# last finding, so every reported line stays the same. A call before the
# definition, a directive, and a heredoc need the metadata scan.
unset KOSH_FLAGS
BIN=$(cd "$(dirname "$BIN")" && pwd -P)/$(basename "$BIN")
start_directory=$(pwd -P)
root=$TEST_TEMP_DIRECTORY/analysis-streamed-units
mkdir -p "$root/short" "$root/long"
root=$(cd "$root" && pwd -P)
trap 'cd "$start_directory" && [ -n "$root" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$root"' EXIT

cat > "$root/short/script.bash" <<'EOF'
#!/bin/bash
greet "$1"
greet() {
  echo $1
}
# shellcheck disable=SC2086
echo $2
cat <<DOC
$3
DOC
echo $unknown_name
EOF

cp "$root/short/script.bash" "$root/long/script.bash"
i=0
while [ "$i" -lt 140000 ]; do
  printf ':\n'
  i=$((i + 1))
done >> "$root/long/script.bash"

long_byte_count=$(wc -c < "$root/long/script.bash" | tr -d ' ')
[ "$long_byte_count" -gt 262144 ] && echo "long-script-past-limit=yes"

cd "$root/short" || exit 1
"$BIN" --lint script.bash > "$root/short.out" 2>&1
echo "short-status=$?"
cd "$root/long" || exit 1
"$BIN" --lint script.bash > "$root/long.out" 2>&1
echo "long-status=$?"

cat "$root/short.out"
if diff "$root/short.out" "$root/long.out" > /dev/null; then
  echo "same-findings=yes"
else
  echo "same-findings=no"
  diff "$root/short.out" "$root/long.out"
fi
