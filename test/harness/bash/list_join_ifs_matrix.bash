#!/bin/bash
# Unquoted $@, $*, ${a[@]}, and ${a[*]} join their elements with the first
# character of IFS and then split, so a non-whitespace IFS keeps empty
# fields, and the nested word of a default operator, the [[ operand, and an
# empty IFS follow their own rules. Checked against bash.
export LC_ALL=C
show() { printf '<%s>' "$@"; echo; }

for ifs in ':' 'x' ' :' '' ' '; do
  IFS=$ifs
  echo "== IFS=[$ifs]"
  for values in 'a b c' 'a b ' 'a  c' ' a b' 'a  ' '  ' ' '; do
    case $values in
      'a b c') arr=(a b c) ;;
      'a b ') arr=(a b '') ;;
      'a  c') arr=(a '' c) ;;
      ' a b') arr=('' a b) ;;
      'a  ') arr=(a '' '') ;;
      '  ') arr=('' '') ;;
      ' ') arr=('') ;;
    esac
    set -- "${arr[@]}"
    echo "-- ${#arr[@]} [$values]"
    show $@
    show $*
    show ${arr[@]}
    show ${arr[*]}
    show ${@#q}
    show ${*#q}
    show ${arr[@]#q}
    show ${arr[*]#q}
    show ${arr[@]/q/Q}
    show ${arr[*]/q/Q}
    show ${arr[@]^^}
    show ${arr[@]:0}
    show ${arr[*]:0}
    show ${u:-$@}
    show ${u:-$*}
    show ${u:-${arr[@]}}
    show ${u:-${arr[*]}}
    show ${u:-${arr[@]#q}}
    show ${u:-${arr[*]#q}}
    show ${u:-${arr[@]/q/Q}}
    show ${u:-${arr[*]/q/Q}}
    show ${u:-${arr[@]:0}}
    show ${u:-${arr[*]:0}}
    show ${u:-${arr[*]:-d}}
    for i in ${arr[@]#q}; do echo -n "($i)"; done; echo
    for i in ${arr[*]#q}; do echo -n "($i)"; done; echo
    v=(${arr[@]#q})
    show "${v[@]}"
    [[ ${arr[@]#q} == "a b" ]] && echo lhs-drop
    [[ ${arr[@]/q/Q} == "a b" ]] && echo lhs-drop
  done
done
