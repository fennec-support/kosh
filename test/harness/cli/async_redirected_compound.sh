unset KOSH_FLAGS
echo "== a redirected group runs in the background:"
"$BIN" --mood bash -c '{ /bin/sleep 5; } > /dev/null 2>&1 &
job=$!
if kill -0 "$job" 2> /dev/null; then echo running; else echo not-running; fi
kill "$job"
wait "$job"
echo "status=$?"' 2>&1
echo "== a redirected subshell runs in the background:"
"$BIN" --mood bash -c '( /bin/sleep 5 ) > /dev/null 2>&1 &
job=$!
if kill -0 "$job" 2> /dev/null; then echo running; else echo not-running; fi
kill "$job"
wait "$job"
echo "status=$?"' 2>&1
echo "== the shell exits without waiting for a redirected background job:"
for mood in bash default; do
  start=$SECONDS
  "$BIN" --mood "$mood" -c '( /bin/sleep 4 ) > /dev/null 2>&1 &'
  elapsed=$((SECONDS - start))
  if [ "$elapsed" -lt 3 ]; then echo "$mood=prompt"; else echo "$mood=waited"; fi
done
