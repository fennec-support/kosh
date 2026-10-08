#!/bin/bash

# A quoted ${arr[@]OP} or ${@OP} inside the word of another expansion keeps
# one field for each element, and a quoted ${arr[@]@A} or ${@@A} splits like
# a declaration. A star form joins into one field.

p() { printf '<%s>' "$@"; echo; }

arr=(a "b c" "" d)
for op in '' ':-d' '-d' '#?' '%?' '/a/A' '//[ab]/-' '^^' ',,' '~' '@Q' '@U' '@a' \
  ':1' ':1:1' ': -2:1'; do
  for sub in @ '*'; do
    eval "p \"\${u:-\${arr[$sub]$op}}\""
  done
done

IFS=:
p "${u:-${arr[*]}}"
p "${u:-${arr[*]#?}}"
p "${u:-${arr[@]#?}}"
unset IFS

empty=()
one=('')
two=('' '')
p "${u:-${empty[@]#?}}" end
p "${u:-${one[@]:-d}}" "${u:-${one[@]-d}}"
p "${u:-${two[@]:-d}}" "${u:-${two[@]-d}}"

set -- x "y z"
p "${u:-$@}"
p "${u:-${@#?}}"
p "${u:-${*#?}}"
p "${u:-${@@Q}}"
p "${u:-${@@a}}"
p "${u:-${@:1}}"
p "${u:-pre${arr[@]}post}"
p "${u:-"${arr[@]}"}"

declare -A map=([k]="v w")
s="sc ex"
p "${arr[@]@A}"
p "${arr[*]@A}"
p "${map[@]@A}"
p "${s@A}"
p "${s[@]@A}"
p "${u@A}"
p "${unsetarr[@]@A}"
p "${arr[0]@A}"
p "${@@A}"
p "${*@A}"
p "${@@a}"
p "${*@a}"
p "${u:-${arr[@]@A}}"
p "${u:-${@@A}}"
