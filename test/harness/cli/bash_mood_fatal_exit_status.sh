unset KOSH_FLAGS
start=$PWD
dir=$(mktemp -d)
trap 'cd "$start" && rm -rf "$dir"' EXIT
cd "$dir"
printf 'set -u\necho "$undefined_variable"\necho after\n' > setu.sh
printf 'echo "${missing:?is gone}"\necho after\n' > report.sh
echo "== a bash-mood set -u abort exits 1 like a bash script, not 127 =="
"$BIN" --mood bash setu.sh
echo "rc=$?"
echo "== a bash-mood \${name:?} abort exits 1 like a bash script =="
"$BIN" --mood bash report.sh
echo "rc=$?"
trap_line='trap '\''echo "trap saw $?"'\'' EXIT'
printf '%s\n: "${CFG?must be set}"\necho after\n' "$trap_line" > trap_report.sh
printf '%s\nset -u\necho "$undefined_variable"\necho after\n' "$trap_line" \
  > trap_setu.sh
printf '%s\nx=0\necho $((1/x))\necho after\n' "$trap_line" > trap_arith.sh
echo "== the EXIT trap reads the status a fatal expansion error ends with =="
for mood in kosh bash bash-posix sh; do
  for script in trap_report.sh trap_setu.sh trap_arith.sh; do
    "$BIN" --mood "$mood" "$script" 2>/dev/null
    echo "$mood $script rc=$?"
  done
done
