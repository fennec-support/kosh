#!/bin/bash
# The export attribute on indexed and associative arrays: the array stays,
# the attribute shows in declare -p and ${arr@a}, and no child environment
# receives the array.
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
