#!/bin/bash
# An asynchronous command forks before it expands its words, so an expansion
# error fails the job rather than the shell, $! names the job, and wait
# reports its status. An expansion side effect stays in the job.
x=abc
echo ${x!y} &
pid=$!
wait "$pid"
echo "bad-substitution=$? job=${pid:+yes}"
echo $(( 1 + )) &
wait $!
echo "arithmetic=$?"
( set -u; echo "$unset_name" & wait $!; echo "nounset=$?" )
echo "${unset_name:?gone}" &
wait $!
echo "question=$?"
readonly r=1
r=2 &
wait $!
echo "readonly=$?"
r=2 echo prefix &
wait $!
echo "readonly-prefix=$?"
shopt -s failglob
echo /nonexistent-dir/*.none &
wait $!
echo "failglob=$?"
shopt -u failglob
for v in 1; do echo $(( 1 / 0 )) & done
wait $!
echo "loop=$?"
f() { echo "in $1"; }
f ${x!y} &
wait $!
echo "function=$?"

i=0
echo "$((i++))" &
wait
echo "i=$i"
: "${assigned:=set}" &
wait
echo "assigned=${assigned-unset}"
plain=1 &
wait
echo "plain=${plain-unset} job=${!:+yes}"
out=${TMPDIR:-/tmp}/async_expansion_error_job.$$
n=5
echo "n=$n" >"$out" &
wait
cat "$out"
rm -f "$out"
z=7 printenv z &
wait
true &
wait $!
echo "true=$?"
false &
wait $!
echo "false=$?"
echo end
