#!/bin/bash
# A here-document body runs a ${ command; } substitution in the shell and
# takes its output, and a ${| command; } substitution yields the REPLY its body
# sets, local to that body. A brace group, a quoted brace, a nested ${...}, and
# a $(...) inside the body keep the substitution open, and ${name@P} expands
# both forms as a prompt does. The commands are builtins, so bash expands the
# body in the shell itself.
f() { echo hook; }
REPLY=outer
read -r line <<E
a${ f; }b ${| REPLY=r; } c
E
echo "1 $line REPLY=$REPLY"
read -r line <<E
${ { echo "{x}"; }; } ${| REPLY="}"; } ${ echo "${line%% *}"; } ${ echo $(echo sub); }
E
echo "2 $line"
read -r line <<E
${ printf '%s|' "one two" three; }
E
echo "3 $line"
read -r line <<'E'
${ f; } stays literal
E
echo "4 $line"
P='a$(f)b${ f; }${| REPLY=v; }'
echo "5 ${P@P}"
x=${| REPLY=word; }
echo "6 $x REPLY=$REPLY"
