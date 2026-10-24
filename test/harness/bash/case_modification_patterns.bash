#!/bin/bash

# A single ^ or , changes at most the first character when it matches the pattern.
# A doubled operator tests every character. The tail after the first character is
# copied unchanged whatever it holds.

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

# A literal or bracket selector limits a doubled operator to the characters it
# matches, and digits pass through unchanged.
upper=ABCDEF
lower=abcdef
word=banana
mixed=AaBbCc
phrase="the quick fox"
n=123abc
echo "${upper,,[ACE]} ${lower^^[bdf]} ${v^^[aeiou]} ${word^^a}"
echo "${mixed^^[a-z]} ${mixed,,[A-Z]} ${phrase^^[tqf]} ${n^^}"
