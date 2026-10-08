#!/bin/bash

# A leading tilde in the word of a parameter expansion expands when it is
# unquoted, in a default, an alternative, a trim pattern, and a replacement
# pattern, but not after an anchor of a replacement.

HOME=/home/zz
x=/home/zz
t() {
  unset u u2 arr
  arr=(1 2)
  eval "printf '<%s>' $1"
  echo
}
while IFS= read -r word; do
  printf '%s: ' "$word"
  t "$word"
done <<'LIST'
${u-~}
"${u-~}"
${u:-~}
${u-~/x}
${u+~}
${HOME+~}
${u=~}
${u2:=~/y}
${u-a:~}
${u-~root}
${u-"~"}
${u-'~'}
${u-\~}
${u-x~}
${u-~:x}
${u:+~}
${arr[5]-~}
${arr[@]+~}
${arr[5]:-~}
${u-~+}
${u-~-}
${u-${u2-~}}
"${u-${u2-~}}"
${u-"${u2-~}"}
${u:-~ ~}
${u-~ ~}
${u-a=~}
${x/~/T}
${x//~/T}
${x/#~/T}
${x/%~/T}
${x/zz/~}
${x/zz/a~}
${x#~}
${x##~}
${x%~}
${x%%~}
${x^~}
LIST
