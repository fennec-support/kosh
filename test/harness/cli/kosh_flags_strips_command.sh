unset KOSH_FLAGS
# A -c in KOSH_FLAGS is dropped during the splice so the environment cannot
# inject a command into every invocation, while a benign flag there survives
# and a real command-line -c still runs.
echo "== -c in KOSH_FLAGS does not run its command:"
KOSH_FLAGS='-c injected_command' "$BIN" -c 'echo clean' 2>&1
# A clustered -c, the long --command forms, a -- that turns the real
# arguments into a script operand, and -s, which reads commands from standard
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
