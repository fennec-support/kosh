# shellcheck disable=unassigned-variable-read
unset KOSH_FLAGS
# caller reports a non-numeric operand inside a call as a located error
# pointing at the operand, and a negative or out of range operand returns 1.
# The values of the frame line are checked against Bash in
# bash/caller_frames.bash.

echo "== caller with a non-numeric operand inside a call is a located error:"
"$BIN" -c 'f() { caller abc; }; f' 2>&1; echo "rc=$?"
echo "== caller with a negative operand inside a call returns 1:"
"$BIN" -c 'f() { caller -- -1; }; f' 2>&1; echo "rc=$?"
echo "== caller with an out of range operand inside a call returns 1:"
"$BIN" -c 'f() { caller 9; }; f' 2>&1; echo "rc=$?"
