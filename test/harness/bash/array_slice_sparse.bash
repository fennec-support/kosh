#!/bin/bash

# A sparse array joins every element in an assignment, a [[ ]] operand, a
# case word, a here-document, and a here-string. A slice of a sparse array
# counts its offset in indices, a negative offset counts back from one past
# the highest index, and an offset before index zero gives nothing. A slice
# of an associative array with one key counts its offset from one, and a
# length of zero still keeps one element.

sp=([1]=one [5]='fi ve' [9]=nine)
v=${sp[@]}; echo "<$v>"
v=${sp[*]}; echo "<$v>"
v="${sp[@]}"; echo "<$v>"
[[ ${sp[*]} == "one fi ve nine" ]]; echo "star cond $?"
[[ ${sp[@]} == "one fi ve nine" ]]; echo "at cond $?"
[[ -n ${sp[*]} ]]; echo "nonempty $?"
case ${sp[*]} in "one fi ve nine") echo "case star";; *) echo "case none";; esac
case ${sp[@]} in "one fi ve nine") echo "case at";; *) echo "case none";; esac
cat <<EOF
here ${sp[*]} | ${sp[@]}
EOF
cat <<< ${sp[*]}
cat <<< "${sp[@]}"
IFS=:
v=${sp[*]}; echo "<$v>"
unset IFS

for offset in 0 1 2 5 6 9 10 -1 -5 -8 -9 -10 -11 0:1 1:1 2:1 1:0 2:2 -2:1; do
  eval "printf '<%s>' \"\${sp[@]: $offset}\"; printf ' [%s]' \"\${sp[*]: $offset}\""
  printf ' %s\n' "$offset"
done
v=${sp[@]:2}; echo "<$v>"
printf '<%s>' ${sp[@]:2}; echo

dense=(a b c)
for offset in -1 -3 -4 -9 -4:2 3 4; do
  eval "printf '<%s>' \"\${dense[@]: $offset}\""
  printf ' %s\n' "$offset"
done
set -- p q r
printf '<%s>' "${@: -3}" "${@: -9}"; echo
printf '<%s>' "${@: -4:1}" | wc -c

declare -A one=([k]=v)
for offset in 0 1 2 -1 -2 -3 0:0 1:0 1:1; do
  eval "printf '<%s>' \"\${one[@]: $offset}\""
  printf ' %s\n' "$offset"
done
declare -A none=()
printf '<%s>' "${none[@]:0}"; echo
