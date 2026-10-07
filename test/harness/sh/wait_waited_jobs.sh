#!/bin/sh
# dash keeps a waited job until the next fork, across input lines and loop
# iterations, and keeps no status once the job is gone. A subshell and a
# command substitution cannot wait for a job of their parent.
(exit 3) & p=$!
x=$(wait $p); echo "substitution=$?"
(wait $p); echo "subshell=$?"
wait $p; echo "first=$?"
wait $p; echo "again=$?"
wait %1; echo "job=$?"
for i in 1 2; do wait $p; echo "loop-$i=$?"; done
/bin/true
wait $p; echo "after-external=$?"

(exit 4) & p=$!
wait; echo "all=$?"
wait $p; echo "after-all=$?"
(true)
wait $p; echo "after-subshell=$?"

(exit 5) & p=$!
wait $p; echo "waited=$?"
true | true
wait $p; echo "after-pipeline=$?"
