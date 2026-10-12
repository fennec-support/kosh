# An interrupt stops a long seq at its next buffered write, so the run ends
# with status 130 well before the KILL fallback of the timeout.
unset KOSH_FLAGS
"$BIN" -c 'koshkit timeout -p -k 10s -s INT 0.2s "$@"' timeout "$BIN" -c \
  'koshkit seq 1 999999999999 > /dev/null'
echo "increasing status=$?"
"$BIN" -c 'koshkit timeout -p -k 10s -s INT 0.2s "$@"' timeout "$BIN" -c \
  'koshkit seq 999999999999 -1 1 > /dev/null'
echo "decreasing status=$?"
