log=$(mktemp)
echo "== trace steers to the descriptor, stdout still shows:"
"$BIN" --mood bash -c "exec 7>'$log'; BASH_XTRACEFD=7; set -x; echo hi; set +x; exec 7>&-" 2>&1
echo "== captured at the descriptor:"
cat "$log"
test -n "$log" && rm -f "$log"
echo "== an external child inherits the numbered descriptor:"
export KOSH_FD_CHILD=$BIN
"$BIN" --mood bash -c "exec 7>'$log'; \"\$KOSH_FD_CHILD\" --no-init-files -c 'printf \"external\\n\" >&7'; exec 7>&-"
cat "$log"
test -n "$log" && rm -f "$log"
echo "== without BASH_XTRACEFD the trace stays on stderr:"
"$BIN" --mood bash -c 'set -x; echo hi; set +x' 2>&1 1>/dev/null
echo "== an unparsable BASH_XTRACEFD falls back to stderr:"
"$BIN" --mood bash -c 'BASH_XTRACEFD=nope; set -x; echo hi; set +x' 2>&1 1>/dev/null
echo "== an array element with a blank traces in single quotes:"
"$BIN" --mood bash -c 'set -x; arr=(one "two three"); set +x' 2>&1
