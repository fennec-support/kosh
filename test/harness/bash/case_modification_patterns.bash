#!/bin/bash

# A single ^ or , changes at most the first character, and only when it
# matches the pattern; a doubled operator tests every character. The tail
# after the first character is copied unchanged whatever it holds.

v=hello
echo "${v^} ${v^^} ${v^[a-h]} ${v^[i-z]} ${v^^[lo]} ${v^h} ${v^e}"
w=HELLO
echo "${w,} ${w,,} ${w,[A-H]} ${w,[I-Z]} ${w,,[LO]}"
m=mIxEd
echo "${m~} ${m~~} ${m^?} ${m,*}"
e=
echo "[${e^}] [${e^^}] [${e,[a]}]"
t='a b  c*?'
echo "${t^} ${t^^[b-c]}"
a=(one two three)
echo "${a[@]^} ${a[*]^^[eo]} ${a[1]^}"
