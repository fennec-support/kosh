#!/bin/bash

# A value or key holding a control byte, an invalid UTF-8 byte, or a
# character that does not print in the locale prints in the $'...' form from
# declare -p, export -p, the @A, @K, and @Q transforms, and printf %q, and the
# printed text evaluates back to the same bytes. A raw 0xff byte in source
# text is an ordinary byte inside and outside quotes.

for locale in C C.UTF-8; do
  LC_ALL=$locale
  echo "== $locale"
  s=$'a\xffb\x80\x01\x1b\t\n\a\b\f\r\v\x7f\'"\\$`'
  u='plain é text'
  n=$'\u0085   ￾ \xc0\x80 \xed\xa0\x80'
  e=''
  for name in s u n e; do
    declare -p "$name"
    printf '%s|%s|%s\n' "${!name@Q}" "${!name@A}" "$(printf %q "${!name}")"
    saved=${!name}
    eval "$(declare -p "$name")"
    [[ ${!name} == "$saved" ]] && echo "declare round trip $name"
    eval "copy=${!name@Q}"
    [[ $copy == "$saved" ]] && echo "quote round trip $name"
    eval "copy=$(printf %q "$saved")"
    [[ $copy == "$saved" ]] && echo "printf round trip $name"
  done

  declare -a list=([2]=$'x\xffy' [5]=plain [7]=$'tab\there' [9]='é')
  declare -p list
  echo "${list[@]@Q}"
  echo "${list[@]@K}"
  echo "${list[@]@A}"
  echo "${list[2]@A}"
  saved_list=("${list[@]}")
  eval "$(declare -p list)"
  [ "${list[*]}" = "${saved_list[*]}" ] && echo "array round trip"

  declare -A map=([$'k\xffey']=$'v\x01' [$'tab\tkey']=é)
  declare -p map
  echo "${map[$'k\xffey']@Q}"
  printf '<%s>' "${map[@]@K}"
  echo
  saved_map=$(declare -p map)
  eval "$saved_map"
  [[ $(declare -p map) == "$saved_map" ]] && echo "map round trip"

  export s
  export -p | grep '^declare -x s='
  export -n s
done

b=$'\xff'
eval "x=\"a${b}b\" y='a${b}b' z=a${b}b w=\$'a${b}b'"
printf '%s' "$x" "$y" "$z" "$w" | od -An -tx1
eval "f${b}() { echo \"function f${b}\"; }; f${b}" | od -An -tx1
eval "v=\`echo q${b}\`; echo \"\$v\" \$((1))${b}" | od -An -tx1
eval "cat <<EOF
h${b}d \$((2))${b}
EOF" | od -An -tx1
