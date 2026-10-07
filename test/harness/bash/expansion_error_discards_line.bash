#!/bin/bash
# A failglob error, a bad substitution, and an assignment to a readonly
# variable abandon the rest of the input line, as an arithmetic error does.
# A readonly loop variable fails only the loop, a readonly prefix assignment
# still runs its command, and set -u or ${name:?} ends the script. In POSIX
# mode a bad substitution and a readonly assignment end the shell instead,
# while failglob still abandons the line.
x=abc
readonly r=1
echo a ${x!y}; echo same
echo "bad-bang=$?"
echo a ${x:}; echo same
echo "bad-colon=$?"
echo a ${%}; echo same
echo "bad-name=$?"
echo a ${x.y}; echo same
echo "bad-operator=$?"
f() { echo in; echo ${x!y}; echo after; }
f; echo same
echo "function=$?"
true && echo ${x!y} || echo or; echo same
echo "and-or=$?"
for i in 1 2; do echo "i$i"; echo ${x!y}; echo after; done; echo same
echo "loop=$?"
z=$(echo ${x!y}; echo after); echo "substitution=[$z] $?"
( echo ${x!y}; echo after ); echo "subshell=$?"
eval 'echo ${x!y}; echo after'; echo "eval=$?"
echo ${x!y} | cat; echo "pipeline=$? ${PIPESTATUS[*]}"
[[ ${x!y} ]]; echo same
echo "conditional=$?"
case ${x!y} in *) echo matched ;; esac; echo same
echo "case=$?"
cat <<EOF; echo "heredoc=$?"
${x!y}
EOF

r=2; echo same
echo "readonly=$?"
r=(1 2); echo same
echo "readonly-array=$?"
r+=3; echo same
echo "readonly-append=$?"
r[1]=3; echo same
echo "readonly-element=$?"
a=1 r=2 b=3; echo same
echo "readonly-multiple=$?"
readonly unset_readonly
: ${unset_readonly:=2}; echo same
echo "readonly-default=$?"
g() { echo in; r=2; echo after; }
g; echo same
echo "readonly-function=$?"
r=2 echo prefix; echo "readonly-prefix=$?"
r=2 :; echo "readonly-prefix-special=$?"
for r in 1; do echo loop; done; echo "readonly-for=$?"
declare r=2; echo "readonly-declare=$?"
echo "r=$r"

shopt -s failglob
echo /nonexistent-dir/*.none; echo same
echo "failglob=$?"
for g in /nonexistent-dir/*.none; do echo "$g"; done; echo same
echo "failglob-for=$?"
echo hi >/nonexistent-dir/*.none; echo same
echo "failglob-redirect=$?"
z=$(echo /nonexistent-dir/*.none; echo after); echo "failglob-sub=[$z] $?"
shopt -u failglob

( set -o posix; shopt -s failglob; echo /nonexistent-dir/*.none; echo same
echo "posix-failglob=$?" )
( set -o posix; echo ${x!y}; echo same ); echo "posix-bad=$?"
( set -o posix; eval 'echo ${x!y}'; echo same ); echo "posix-bad-eval=$?"
( set -o posix; r=2; echo same ); echo "posix-readonly=$?"
( set -o posix; r=2 echo prefix; echo same
echo "posix-prefix=$?" )
( set -o posix; r=2 :; echo same ); echo "posix-prefix-special=$?"
( set -o posix; for r in 1; do :; done; echo same ); echo "posix-for=$?"
( set -o posix; eval 'for r in 1; do :; done'; echo same )
echo "posix-for-eval=$?"
( set -o posix; : ${unset_readonly:=2}; echo same ); echo "posix-default=$?"
( set -u; echo $unset_name; echo same ); echo "nounset=$?"
( echo ${unset_name:?gone}; echo same ); echo "question=$?"
echo end
r=2
echo not reached
