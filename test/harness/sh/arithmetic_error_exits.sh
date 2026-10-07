#!/bin/sh
# An arithmetic expansion error ends a non-interactive shell with status 2. A
# subshell, a command substitution, an eval inside a subshell, and a pipeline
# stage end alone and report 2, and double quotes inside the expression are
# rejected the way dash rejects them.
( echo before; echo $(( 1 + )); echo after ); echo "subshell=$?"
x=$(echo before; echo $(( 1 + )); echo after); echo "substitution=[$x] $?"
( eval 'echo $(( 1 + ))'; echo after ); echo "eval=$?"
( f() { echo $(( 1 + )); echo after; }; f; echo after-call ); echo "function=$?"
( for i in 1 2; do echo $(( 1 + )); done; echo after ); echo "loop=$?"
echo $(( 1 + )) | cat; echo "pipeline=$?"
( echo $(( "1" + 2 )) ); echo "quoted=$?"
( y='"1"'; echo $(( $y )) ); echo "quoted-value=$?"
echo end
echo $(( 1 + ))
echo not reached
