#!/bin/bash
# The export attribute on indexed and associative arrays: the array stays,
# the attribute shows in declare -p and ${arr@a}, and no child environment
# receives the array. An exported scalar that becomes an array leaves the
# environment, and a sibling export stays. export -n also keeps a dynamic
# variable dynamic, removes a function from the environment, and refuses an
# invalid name.
export LC_ALL=C

show_environment() {
  local line
  while IFS= read -r line; do
    [[ $line == arr=* ]] && echo "env: $line"
  done < <(env)
  echo "env done"
}

echo "== export keeps an indexed array"
arr=(a b)
export arr
declare -p arr
echo "[${arr@a}]"
show_environment

echo "== export with a value sets element 0"
export arr=v
declare -p arr
echo "[${arr@a}]"
show_environment

echo "== the attribute survives writes"
arr+=(c)
declare -p arr
arr=(z)
declare -p arr
unset 'arr[0]'
declare -p arr

echo "== export -n and declare +x remove the attribute"
arr=(a b)
export arr
export -n arr
declare -p arr
export arr
declare +x arr
declare -p arr

echo "== declare -x forms"
unset arr
declare -x arr=(p q)
declare -p arr
show_environment
unset arr
arr=(1 2)
declare -x arr
declare -p arr
unset arr
declare -xa arr
declare -p arr

echo "== associative arrays"
unset arr
declare -A arr=([0]=v)
export arr
declare -p arr
echo "[${arr@a}]"
export arr=w
declare -p arr
echo "[${arr@a}]"
show_environment
export -n arr
declare -p arr

echo "== unset removes the attribute"
arr=(a b)
export arr
unset arr
arr=(c)
declare -p arr

echo "== a child shell sees no array"
arr=(a b)
export arr
"$BASH" -c 'declare -p arr 2>/dev/null; echo "status $?"'

echo "== a subshell inherits the attribute"
( declare -p arr )

echo "== export -p lists exported arrays"
declare -A listed_map=([k]=v)
export listed_map
export -p | grep -e ' arr=' -e ' listed_map='
export -n listed_map
export -p | grep -c ' listed_map='

# export without a value marks an unset name and leaves it unset, so it reaches
# the environment only when assigned.
export unset_export
echo "unset-export=[${unset_export-unset}]"
export -p | grep -c ' unset_export$'
unset_export=assigned
env | grep '^unset_export='

echo "== an exported scalar becomes an array"
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

echo "== export -n keeps RANDOM dynamic"
export RANDOM
export -n RANDOM
first=$RANDOM
result=frozen
for value in $RANDOM $RANDOM $RANDOM $RANDOM $RANDOM; do
  if [[ $value != "$first" ]]; then
    result=dynamic
    break
  fi
done
echo "$result"

echo "== export -nf removes the function from the environment"
f() { :; }
export -f f
env | grep -q '^BASH_FUNC_f%%='
echo "exported=$?"
export -nf f
env | grep -q '^BASH_FUNC_f%%='
echo "unexported=$?"
f
echo "call=$?"

echo "== export -n refuses an invalid name"
export -n 'a-b' 2>/dev/null
echo "status=$?"
