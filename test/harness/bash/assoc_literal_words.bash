#!/bin/bash

# A word in an associative array list is neither split nor globbed, and a
# quoted array expansion joins into one word.

cd "$(mktemp -d)" || exit 1
touch ga gb

dump() {
  local -n map=$1
  local key
  for key in "${!map[@]}"; do echo "<$key>=<${map[$key]}>"; done | sort
  echo --
}

y=("a b" "c*" "")
declare -A x=("${y[@]}")
dump x
declare -A g=(*)
dump g
v="p q"; e=
declare -A h=($v r)
dump h
declare -A h2=(a $e)
dump h2
declare -A i=([k]=$v [j]="a b")
dump i
declare -A j=("$v" r)
dump j
declare -A m; m=(*.nomatch b x*)
dump m
m+=(ga "$v" $v)
dump m
set -- 1 2 3 4
declare -A p=("$@")
dump p
declare -A q=($@)
dump q
IFS=:
declare -A r=("$@" "${y[*]}" "${y[@]}" x)
dump r
f() { local -A z=($v w); dump z; }
f
declare -A n
n=(*)
dump n
