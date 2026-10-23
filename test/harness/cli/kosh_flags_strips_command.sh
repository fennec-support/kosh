unset KOSH_FLAGS
# A -c in KOSH_FLAGS is dropped during the splice so the environment cannot
# inject a command into every invocation, while a benign flag there survives
# and a real command-line -c still runs.
echo "== -c in KOSH_FLAGS does not run its command:"
KOSH_FLAGS='-c injected_command' "$BIN" -c 'echo clean' 2>&1
# A clustered -c, the long --command forms, a -- that turns the real
# arguments into a script operand, and -s, which takes commands from standard
# input, are dropped the same way.
for form in '-xc injected_command' '--command injected_command' \
  '--command=injected_command' '-- injected_script' '-s' '--stdin'; do
  printf '== %s: ' "$form"
  KOSH_FLAGS=$form "$BIN" -c 'echo clean' 2>&1
done
echo "== benign flag in KOSH_FLAGS survives:"
KOSH_FLAGS='--mood sh' "$BIN" -c 'set -M; echo flagged' 2>&1
echo "rc=$?"
echo "== a short flag keeps its separate value:"
KOSH_FLAGS='-M sh' "$BIN" -c 'set -M' 2>&1
# The kosh mood loads no rc file, so the rc cases run in the bash mood.
echo "== an rc file from KOSH_FLAGS is not sourced:"
rc_file=$TEST_TEMP_DIRECTORY/kosh-flags-rcfile
printf 'echo rcfile-sourced\n' > "$rc_file"
for label in separate attached init-file; do
  case $label in
  separate) form="--rcfile $rc_file" ;;
  attached) form="--rcfile=$rc_file" ;;
  init-file) form="--init-file $rc_file" ;;
  esac
  sourced_count=$(printf 'exit\n' |
    KOSH_FLAGS=$form "$BIN" --mood bash -i 2>&1 | grep -c '^rcfile-sourced$')
  printf '%s: sourced=%s\n' "$label" "$sourced_count"
done
rm -f "$rc_file"
echo "== the other letters of a cluster with c survive:"
KOSH_FLAGS='-ec injected_command' "$BIN" -c \
  'case $- in *e*) echo errexit-kept ;; *) echo errexit-lost ;; esac' 2>&1
