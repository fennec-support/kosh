# A bad substitution and an assignment to a readonly variable end the shell
# with status 2, through an eval as well, as dash does.
x=abc
readonly r=1
( echo ${x!y}; echo same ); echo "bad=$?"
( eval 'echo ${x:}'; echo same ); echo "bad-eval=$?"
( r=2; echo same ); echo "readonly=$?"
( eval 'r=2'; echo same ); echo "readonly-eval=$?"
( for r in 1; do :; done; echo same ); echo "readonly-for=$?"
( eval 'for r in 1; do :; done'; echo same ); echo "readonly-for-eval=$?"
y=$(r=2; echo same); echo "substitution=[$y] $?"
echo end
r=2
echo not reached
