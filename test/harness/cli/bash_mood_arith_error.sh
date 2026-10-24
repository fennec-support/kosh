unset KOSH_FLAGS
echo "== bash mood reports an arithmetic error and goes on to the next line, matching bash:"
"$BIN" --mood bash -c 'echo before
echo $((1/0))
echo after'
echo "rc=$?"
echo "== the strict default mood aborts the run on an arithmetic error:"
"$BIN" -c 'echo $((1/0)); echo after'
echo "rc=$?"
echo "== the strict default mood aborts on an unset arithmetic operand:"
"$BIN" --mood kosh -c 'echo before; echo $((nope + 1)); echo not-reached'
echo "rc=$?"
echo "== bash mood reports a stray token after an operand before evaluating:"
"$BIN" --mood bash -c 'n=src/Koshkit.cpp; echo $((n-2))' 2>&1 | grep -c "Unexpected '.cpp' after the expression"
echo "== bash mood soft errors keep running:"
echo "-- a soft builtin error carries a located caret"
"$BIN" --mood bash -c 'builtin nosuchbuiltin'
echo "rc=$?"
echo "-- the run keeps going past the soft error"
"$BIN" --mood bash -c 'builtin nosuchbuiltin; echo after'
echo "-- a thrown builtin error is located and soft too"
"$BIN" --mood bash -c 'read -Z; echo after'
