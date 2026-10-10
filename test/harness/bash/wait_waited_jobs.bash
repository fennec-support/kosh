#!/bin/bash
# A child shell cannot wait for a job of its parent. A subshell, a pipeline
# stage, and an asynchronous command answer 127, while a command substitution
# still reports the saved status of a job its parent waited. A job waited by
# number or process stays in the table until the next input line, a loop
# iteration, a fork, or the jobs builtin, so a second wait for it on the same
# line reports its status again. wait -n reports an unknown target and still
# waits for a known one.
(exit 3) & p=$!
wait $p; echo "first=$?"
x=$(wait $p); echo "substitution=$?"
(wait $p); echo "subshell=$?"
{ wait $p; echo "stage=$?"; } | cat
wait $p & wait $!; echo "async=$?"
wait $p; echo "again=$?"

sleep 5 & running=$!
x=$(wait $running); echo "running-substitution=$?"
(wait $running); echo "running-subshell=$?"
(wait %1); echo "running-subshell-job=$?"
kill $running
wait $running 2>/dev/null
echo "killed=$?"

(exit 4) &
wait %1; echo "line-a=$?"; wait %1; echo "line-b=$?"
wait %1; echo "next-line=$?"
(exit 4) &
{ wait %1; echo "group-a=$?"; wait %1; echo "group-b=$?"; }
(exit 4) &
f() { wait %1; echo "function-a=$?"; wait %1; echo "function-b=$?"; }
f
f
(exit 4) &
for i in 1 2; do wait %1; echo "loop-$i=$?"; done
(exit 4) &
wait %1; echo "before-external=$?"; /bin/true; wait %1; echo "after-external=$?"
(exit 4) &
wait %1; /no-such-dir/true 2>/dev/null; wait %1; echo "after-missing-path=$?"
(exit 4) &
wait %1; no-such-command-zz 2>/dev/null; wait %1; echo "after-missing-name=$?"
(exit 4) &
wait %1; echo "before-pipeline=$?"; true | true; wait %1; echo "after-pipeline=$?"
(exit 4) &
wait %1; echo "before-substitution=$?"; y=$(true); wait %1; echo "after-substitution=$?"
(exit 4) &
wait %1; echo "before-jobs=$?"; jobs; wait %1; echo "after-jobs=$?"
(exit 4) & q=$!
wait $q; echo "pid=$?"; wait %1; echo "job-after-pid=$?"
(exit 4) &
wait %1; echo "before-eval=$?"; eval 'wait %1'; echo "eval=$?"
(exit 4) &
wait %1; echo "before-n=$?"; wait -n; echo "n=$?"; wait %1; echo "after-n=$?"
(exit 4) &
wait %1; echo "waited=$?"; true & wait %1; echo "new-job=$?"
wait

(exit 5) & r=$!
wait -n 99999 $r; echo "unknown-then-known=$?"
(exit 5) & r=$!
wait -n $r %9; echo "known-then-bad-job=$?"
(exit 5) & r=$!
wait -n word $r; echo "word=$?"
wait -n 99999; echo "unknown-only=$?"
(exit 5) & r=$!
wait $r
wait -n 99999 $r; echo "saved-status=$?"
