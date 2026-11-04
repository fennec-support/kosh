unset KOSH_FLAGS
# A malformed [[ =~ ]] regex points its caret at the regex operand and shows the
# offending pattern, rather than pointing the bare [[. In the kosh mood it
# ends the shell with status 2.
"$BIN" -c '[[ abc =~ ( ]]' 2>&1
echo "status $?"
