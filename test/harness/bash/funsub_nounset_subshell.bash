#!/bin/bash

# An unset variable under set -u inside a function substitution ends the
# enclosing subshell, while a command substitution contains the error.

( set -u; echo "${ echo hi; }"; echo in; echo "${nov}"; echo not ); echo "after=$?"
( set -u; echo "${ echo ${nov}; }"; echo not8 ); echo "a8=$?"
( set -u; echo "${| REPLY=${nov}; }"; echo not9 ); echo "a9=$?"
( set -u; echo ${ echo ${nov}; } ; echo not10 ); echo "a10=$?"
( set -u; echo "$(echo ${nov})"; echo yes7 ); echo "a7=$?"
( set -u; v=${ echo ${nov}; }; echo not11 ); echo "a11=$?"
