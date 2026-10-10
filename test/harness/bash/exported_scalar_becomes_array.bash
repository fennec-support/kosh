#!/bin/bash
# An exported scalar that becomes an indexed or associative array leaves the
# environment of every child, as in bash, since an array is never exported.
export XX=1
XX=(a b)
env | grep '^XX=' || echo "XX absent"
export YY=1
declare -a YY
env | grep '^YY=' || echo "YY absent"
export WW=1
declare -A WW
env | grep '^WW=' || echo "WW absent"
export ZZ=1
ZZ[3]=x
env | grep '^ZZ=' || echo "ZZ absent"
echo "ZZ=${ZZ[*]}"
export UU=first
declare -A UU
declare -p UU
export VV=kept
env | grep '^VV='
