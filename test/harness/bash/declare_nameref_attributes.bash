#!/bin/bash

# The integer and case marks of declare on a nameref land on the variable it
# refers to, so a later or the same assignment converts the target.

x=1; declare -n r=x; declare -i r; r=3+3; echo "a $x"; declare -p x r
y=1; declare -n s=y; declare -i s=3+3; echo "b $y"; declare -p y s
z=1; declare -n t=z; declare -u t=abc; echo "c $z"; declare -p z t
w=1; declare -n v=w; declare -i v; declare v=4+4; echo "d $w"
declare -n u1=q1; declare -i u1=2+2; echo "e ${q1-unset}"; declare -p u1 q1
l=ABC; declare -n m=l; declare -l m; m=DEF; echo "f $l"
