unset KOSH_FLAGS
# As process 1 in a fresh PID namespace, the shell reaps orphans it did not
# start, keeps the statuses of its own pipeline stages, and exits with status
# 143 after SIGTERM, running the EXIT trap once.
run_as_pid_one() {
  unshare --map-root-user --pid --fork --mount-proc "$BIN" --mood bash -c "$1"
}

echo "== lastpipe stages keep their statuses:"
run_as_pid_one 'shopt -s lastpipe
(exit 7) | { sleep 0.2; :; :; }
echo "ps=${PIPESTATUS[*]}"
printf "a\nb\n" | while read -r line; do sleep 0.1; last=$line; done
echo "last=$last ps=${PIPESTATUS[*]}"'

echo "== a pending process substitution keeps its status:"
run_as_pid_one 'while read -r line; do sleep 0.1; echo "$line"; done \
  < <(printf "x\n"; exit 3)
wait $!
echo "rc=$?"'

echo "== orphans are reaped while the shell waits:"
run_as_pid_one '/bin/sh -c "for i in 1 2 3; do (sleep 0.05 &); sleep 0.1; done
sleep 0.3" &
wait
sleep 0.2
zombie_count=0
for stat in /proc/[0-9]*/stat; do
  read -r _ _ state _ < "$stat" 2>/dev/null || continue
  [[ $state == Z ]] && zombie_count=$((zombie_count + 1))
done
echo "zombies=$zombie_count"'

echo "== SIGTERM ends the foreground command and exits with 143:"
run_as_pid_one 'trap "echo exit-trap" EXIT
(sleep 0.3; kill -TERM 1) &
/bin/sh -c "sleep 5"
echo not-reached' 2>/dev/null
echo "rc=$?"

echo "== a killed subshell dies without signalling other processes:"
run_as_pid_one '( sleep 5 ) & victim=$!
sleep 1 & bystander=$!
sleep 0.2
kill $victim
wait $victim
echo "victim=$?"
wait $bystander
echo "bystander=$?"'
