# An error of a regular builtin fails it with status 2 and the script goes on.
# An error of a special builtin or of local ends the shell with status 2, and
# command keeps the shell running. A bad trap signal only fails trap. Extra
# operands of exit, break, and continue are ignored, as dash does. A local
# through command binds nothing and succeeds even outside a function. read
# without a variable, printf without a format, an unknown job for wait or
# jobs, and a bad ulimit operand fail with status 2. times prints six
# decimals.
set -- one
cd /nonexistent; echo "cd=$?"
[ a b c ]; echo "bracket=$?"
[ -R x ]; echo "bracket-R=$?"
test -Q x; echo "test=$?"
read -Z x </dev/null; echo "read=$?"
kill -s NOPE 1; echo "kill=$?"
umask 999; echo "umask=$?"
trap 'echo trap' BADSIG; echo "trap=$?"
( shift 5; echo same ); echo "shift=$?"
( export 1a=b; echo same ); echo "export=$?"
( readonly 1a=b; echo same ); echo "readonly=$?"
( readonly r=1; unset r; echo same ); echo "unset-readonly=$?"
( unset 1a; echo same ); echo "unset=$?"
( exit abc; echo same ); echo "exit=$?"
( exit 3 4; echo same ); echo "exit-extra=$?"
( for i in 1; do break 0; done; echo same ); echo "break=$?"
( break abc; echo same ); echo "break-outside=$?"
( continue 0; echo same ); echo "continue-outside=$?"
( for i in 1 2; do continue 1 2; echo same; done; echo looped ); echo "continue-extra=$?"
( f() { return abc; }; f; echo same ); echo "return=$?"
( f() { local 1a; }; f; echo same ); echo "local=$?"
( f() { shift 5; echo same; }; f; echo same ); echo "shift-function=$?"
command shift 5; echo "command-shift=$?"
command export 1a=b; echo "command-export=$?"
command unset 1a; echo "command-unset=$?"
x=outer
command local x=inner; echo "command-local=$? x=$x"
command local 1a; echo "command-local-bad=$?"
g() { command local x=inner; echo "command-local-function x=$x"; }
g
read </dev/null; echo "read-no-name=$?"
printf; echo "printf-no-format=$?"
printf -v x; echo "printf-option=$?"
wait %9; echo "wait-job=$?"
jobs %9; echo "jobs-job=$?"
( ulimit -n abc; echo "ulimit-number=$?" )
( ulimit -S -n 1000 2000; echo "ulimit-extra=$?"; ulimit -S -n )
times | sed 's/[0-9]/N/g'
shift 5
echo not reached
