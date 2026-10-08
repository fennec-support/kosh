# An ampersand ends a whole and-or list, so the left side of && and || runs in
# the background job as well, and the job status is that of the whole list.
sleep 0.3 && echo "left-done" &
echo "after-and"
wait
sleep 0.3 || echo never &
echo "after-or"
wait
false || sleep 0.3 && echo "chain-done" &
echo "after-chain"
wait
! false && sleep 0.3 && echo "negated-done" &
echo "after-negated"
wait
sleep 0.3 && echo piped | cat && echo "pipeline-done" &
echo "after-pipeline"
wait
sleep 0.3 && echo first-job & sleep 0.1 && echo second-job &
echo "after-two"
wait
x=1
d=$PWD
true && x=2 && cd / &
wait
echo "x=$x same-dir=$([ "$PWD" = "$d" ] && echo yes)"
true && echo "pid-is-job" &
pid=$!
wait $pid
echo "wait=$?"
false && echo skipped &
wait $!
echo "false-and=$?"
true && false &
wait $!
echo "true-and-false=$?"
false || (exit 4) &
wait $!
echo "subshell=$?"
true && exit 5 &
wait $!
echo "exit=$?"
( set -e; true && false & wait $! || echo "errexit-job=$?" )
f() { sleep 0.2 && echo "in-function" & }
f
echo "after-function"
wait
{ sleep 0.2 && echo "in-group" & }
echo "after-group"
wait
( sleep 0.2 && echo "in-subshell" & )
sleep 0.5
if true; then sleep 0.2 && echo "in-if" & fi
echo "after-if"
wait
for i in 1 2; do sleep "0.${i}5" && echo "loop-$i" & done
echo "after-loop"
wait
true &&
  sleep 0.2 &&
  echo "multiline" &
echo "after-multiline"
wait
echo end
