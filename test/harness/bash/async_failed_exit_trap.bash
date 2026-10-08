#!/bin/bash
# An asynchronous simple command that meets a fatal expansion or arithmetic
# error runs the inherited EXIT trap in its job before the job ends. A group,
# a subshell, an and-or list, and a job that ends normally do not.
trap 'echo "trap $?"' EXIT
: ${unset_name:?gone} &
wait
echo $(( 1 / 0 )) &
wait
n=$(( 1 / 0 )) &
wait
f() { : ${unset_name:?gone} & wait; }
f
{ : ${unset_name:?gone}; } &
wait
true && : ${unset_name:?gone} &
wait
( : ${unset_name:?gone} ) &
wait
false &
wait
exit 3 &
wait
echo end
