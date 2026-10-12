#!/bin/bash

# An indexed subscript with a side effect is evaluated once when a modifier follows the element.
# This applies when the modifier tests, substitutes, or transforms the element.

a=(x y z)
i=0; echo "${a[i++]:-d} $i"
i=0; echo "${a[i++]-d} $i"
i=0; echo "${a[i+=1]:+set} $i"
i=0; echo "${a[i++]@Q} $i"
i=0; echo "${a[i++]^} $i"
i=0; echo "${a[i++]/x/X} $i"
i=0; echo "${a[i++]#x}. $i"
i=5; echo "${a[i++]:-unset} $i"
i=0; echo "${a[--i]:-d} $i"
n=0; echo "${a[$((n++))]:-d} $n"
echo "${a[1]:-d} ${a[-1]:-d} ${a[9]-u}"
i=0; echo "${a["$((i++))"]:-d} $i"
i=0; echo "${a[${i:=1}]:-d} $i"
k=2; echo "${a[$k]:-d} ${a[$k]@Q}"
declare -A m=([k]=v)
echo "${m[k]:-d} ${m[none]:-d}"
x='k++'; k=0; echo "${a[x]:-d} $k"
k=0; echo "${a[x]@Q} $k"
s=scalar; i=0; echo "${s[i++]:-d} $i"
i=0; echo "${s[i+1]-unset} $i"
(set -u; k=7; echo "${a[$k]@Q}") 2>/dev/null; echo "unset status $?"
