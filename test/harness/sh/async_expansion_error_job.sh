# An asynchronous command forks before it expands its words, as dash does, so
# an expansion error ends only the job, with status 2.
x=abc
echo ${x!y} &
pid=$!
wait "$pid"
echo "bad-substitution=$? job=${pid:+yes}"
echo $(( 1 + )) &
wait $!
echo "arithmetic=$?"
( set -u; echo "$unset_name" & wait $!; echo "nounset=$?" )
readonly r=1
r=2 &
wait $!
echo "readonly=$?"
i=0
echo "$((i += 1))" &
wait
echo "i=$i"
echo end
