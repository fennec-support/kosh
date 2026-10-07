#!/bin/bash
# declare -p prints every set element of a sparse indexed array under its own
# index, checked byte-for-byte against bash. The listing forms with -p or an
# attribute letter and no names print the same declarations.
a=(x [3]=y)
declare -p a
b=(x)
b[5]=z
declare -p b
c=(x y z)
unset 'c[0]'
declare -p c
d=()
d[7]=s
declare -p d
e=([2]=two [1]=one)
declare -p e
g=(p q r s)
unset 'g[1]'
g[1]=back
declare -p g
declare -A h=([k]=v)
declare -p h
f() {
  local -a l=(p [4]=q)
  declare -p l
  declare -p | grep '^declare -a l='
}
f
readonly -a r=(1 [2]=3)
declare -p r
declare -p a b c
declare -p | grep -E '^declare -a [ab]='
declare -a | grep -E '^declare -a [ab]='
declare -A | grep '^declare -A h='
declare -r | grep '^declare -ar r='
declare -i n=4
declare -i | grep '^declare -i n='
