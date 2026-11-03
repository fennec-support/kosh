#!/bin/bash
# An array declared without a value lists without parentheses and reports its
# attributes through ${name[@]@a}, until a write gives it a value, checked
# against bash.
declare -a x
declare -A y
declare -a z=()
declare -A w=()
declare -p x y z w
echo "[${x[@]@A}] [${x[@]@a}] [${y[@]@A}] [${y[@]@a}] [${z[@]@a}] [${w[@]@a}]"
y[a]=1
unset 'y[a]'
declare -p y
echo "[${y[@]@A}] [${y[@]@a}]"
x[0]=1
unset 'x[0]'
declare -p x
declare -A v
v=()
declare -p v
declare -A u
declare -p u
echo "[${u[@]@K}] [${u[*]@a}] [${u[*]@A}]"

# Under set -u, an arithmetic element read of a variable without a value is
# an error before the subscript is evaluated, and a missing element of an
# array with a value reads as 0. A plain assignment needs no value.
f() { local -A lu; local -a la; declare -p lu la; }
f
for probe in 'u[k]' 'x[1]' 'nothing[k]' 'nothing[unset_index]' 'u[k]++' \
  'u[k] += 1' 'z[4]' 'w[k]' 'y[k]' 'z[unset_index]' 'u[k] = 2' \
  'BASH_CMDS[k]' 'BASH_ALIASES[k]' 'BASH_VERSINFO[0]'
do
  ( set -u; echo "$probe=$(( $probe ))" ) 2>/dev/null
  echo "status=$?"
done
( set -u; (( u[k] )) ) 2>/dev/null; echo "command status=$?"
( set -u; let 'u[k]' ) 2>/dev/null; echo "let status=$?"
( set -u; [[ u[k] -eq 0 ]] ) 2>/dev/null; echo "test status=$?"
( set -u; g() { local -A lu; echo $(( lu[k] )); }; g ) 2>/dev/null
echo "local status=$?"
( set -u; declare -n to_nothing=nothing; echo $(( to_nothing[k] )) ) 2>/dev/null
echo "nameref status=$?"
