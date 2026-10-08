# An error of a regular builtin fails it with status 2 and the script goes on.
# An error of a special builtin or of local ends the shell with status 2, and
# command keeps the shell running. A bad trap signal only fails trap. Extra
# operands of exit, break, and continue are ignored, as dash does.
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
shift 5
echo not reached
