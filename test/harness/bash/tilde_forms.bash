#!/bin/bash
# The tilde prefix expands only when wholly unquoted, ~+ and ~- read PWD and
# OLDPWD, and an unknown user stays literal, the lines tilde.tests runs. A tilde
# after a colon expands in an assignment value only.
echo ~
echo ~/foo
echo ~/"foo"
echo ~ch\et
echo ~ch\et/foo
echo "~chet"/"foo"
echo \~chet/"foo"
echo ~\chet/bar
echo ~chet""/bar
echo ~nosuchuserhopefully/bar
cd /usr && cd / && echo "plus=$(cd /tmp && echo ~+)" "minus=$(echo ~-)"

# A bare tilde before a colon expands the same as bash. In an assignment value
# each colon-delimited segment expands its leading tilde, and a regular word
# expands only its own leading tilde while a tilde after a colon stays literal.
# A quoted tilde is left alone.
p=~:~; echo "$p"
x=~/a:~/b; echo "$x"
a=~/x b=~/y; echo "$a $b"
echo ~:x
echo ~/a:~/b
echo ~:~
echo a:~
echo "~:x"
