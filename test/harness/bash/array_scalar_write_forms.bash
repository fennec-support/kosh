#!/bin/bash
# A scalar write to an array sets element 0 or key 0, an array append to a
# scalar keeps the scalar at index 0, and unset of a subscript on a scalar
# removes the scalar for index 0 and fails otherwise.
export LC_ALL=C

show() {
  local declaration
  declaration=$(declare -p "$1" 2>/dev/null) || declaration=unset
  printf '[%s]\n' "$declaration"
}

a=(one two)
a=x
show a
a+=y
show a
read -r a <<< rd
show a
printf -v a %s pv
show a

declare -A m=([k]=v)
m=q
show m
m+=r
show m

s='a b'
s+=(z)
show s

e=
e+=(z)
show e

declare -i n=6
n+=(z)
show n

declare -ai ia
read -ra ia <<< "r1 5"
show ia
mapfile -t ia <<< $'3\n4'
show ia

t=1
unset 't[1]'
echo "status=$?"
show t
unset 't[@]'
echo "status=$?"
show t
unset 't[0]'
echo "status=$?"
show t

declare -A am=([k]=v)
read -ra am <<< "r1 r2"
echo "status=$?"
show am
