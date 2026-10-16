unset KOSH_FLAGS
# suspend stops the shell with SIGSTOP and resumes on SIGCONT. The login-shell
# guard is bypassed with -f. The marker file shows that no command after suspend
# runs before SIGCONT and that the next command runs after it.
d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT
"$BIN" -c 'echo before > "$1"; suspend -f; echo after >> "$1"' shell "$d/marker" &
SUSP_PID=$!
i=0
while [ ! -s "$d/marker" ] && [ "$i" -lt 500 ]; do
  sleep 0.01
  i=$((i + 1))
done
sleep 0.3
echo "== while stopped:"
cat "$d/marker"
kill -CONT "$SUSP_PID" 2>/dev/null
wait "$SUSP_PID"
echo "rc=$?"
echo "== after SIGCONT:"
cat "$d/marker"
