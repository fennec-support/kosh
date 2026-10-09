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
