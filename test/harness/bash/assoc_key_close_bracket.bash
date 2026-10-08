#!/bin/bash

# A quoted ] in an associative key neither closes the subscript nor ends
# the assignment target, in a list, an element assignment, and an expansion.

dump() {
  local -n map=$1
  local key
  for key in "${!map[@]}"; do echo "<$key>=<${map[$key]}>"; done | sort
  echo --
}

declare -A m
m=(["k]x"]=1 ['a]b']=2 [c]=3)
dump m
m['k]x']=9
m["z]=y"]=8
m['w]']+=1
m['w]']+=2
dump m
echo "${m['k]x']} ${m["z]=y"]}"
echo ${m['k]x']} ${m["z]=y"]-none} ${m['missing]']-none}
echo "${m['k]x']:-none} ${m['nope]']:-none} ${#m['k]x']}"
k='q]r'
m[$k]=5
echo "${m[$k]}"
unset "m['k]x']"
unset 'm["z]=y"]'
dump m
m+=(["p]q"]=1)
dump m
[[ -v m['a]b'] ]]; echo "set $?"
[[ -v m['a]b]x'] ]]; echo "unset $?"
