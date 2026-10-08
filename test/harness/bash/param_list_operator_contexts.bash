#!/bin/bash
# An operator after ${a[@]}, ${a[*]}, $@, or $* in a context that expands one
# string, such as [[ ]], case, a here-document, or an assignment. The @ form
# joins with a space and the * form with the first IFS character, a test
# operator reads the joined value, and the other operators apply to each
# element before the join. The field forms keep their own words, and an
# element subscript takes the same transformations and case toggles.

show_contexts() {
  local form=$1 joined
  shift
  eval "joined=\$(cat <<EOF
$form
EOF
)"
  printf '%s here <%s>' "$form" "$joined"
  eval "[[ $form == \"\$joined\" ]]" && printf ' [[ ]]'
  eval "[[ \"$form\" == \"\$joined\" ]]" && printf ' "[[ ]]"'
  eval "case $form in \"\$joined\") printf ' case' ;; esac"
  printf '\n'
}

PROMPT_COMMAND=()
if [[ ";${PROMPT_COMMAND[*]:-};" != *";hook;"* ]]; then
  echo "prompt hook missing"
fi
PROMPT_COMMAND=(hook)
if [[ ";${PROMPT_COMMAND[*]:-};" == *";hook;"* ]]; then
  echo "prompt hook present"
fi

IFS=':;'
for setup in 'unset a' 'a=()' 'a=("")' 'a=("" "")' 'a=(ab "c d" ef)'; do
  eval "$setup"
  echo "== $setup"
  for form in '${a[@]-d}' '${a[*]:-d}' '${a[@]+d}' '${a[*]:+d}' \
    '${a[*]#?}' '${a[*]%%?}' '${a[*]/c/X}' '${a[*]//[ae]/Y}' \
    '${a[*]^}' '${a[*]^^}' '${a[*],,}' '${a[*]@Q}' '${a[*]@U}' \
    '${a[@]@K}' '${a[*]@A}' '${a[*]:1}' '${a[*]:0:2}'; do
    show_contexts "$form"
  done
done

a=(ab "c d" ef)
echo "== @ in a here-document"
cat <<EOF
<${a[@]#?}> <${a[@]%?}> <${a[@]/c/X}> <${a[@]^}> <${a[@]@Q}> <${a[@]@k}>
EOF

echo "== assignments"
x=${a[*]:-d}; echo "<$x>"
x=${a[@]:+d}; echo "<$x>"
x=${a[*]#?}; echo "<$x>"
x="${a[*]@Q}"; echo "<$x>"
x=${missing[*]:-d}; echo "<$x>"
x=${a[@]:=unused}; echo "<$x>"
x=${a[*]:?unused}; echo "<$x>"

echo "== associative array"
declare -A m=([k]=value)
[[ ${m[@]:-d} == value ]] && echo "assoc default"
[[ ${m[*]^} == Value ]] && echo "assoc case"
[[ ${m[@]@K} == 'k "value" ' ]] && echo "assoc key listing"

echo "== positional parameters"
for form in '${@-d}' '${*:-d}' '${@+x}' '${*#?}' '${*/a/X}' '${*^}' \
  '${*@Q}' '${@@Q}' '${@@A}'; do
  show_contexts "$form"
  show_contexts "$form" ""
  show_contexts "$form" ab "c d"
done

echo "== fields"
unset IFS
a=(p "q r")
printf '<%s>' "${a[@]:=x}"; echo
printf '<%s>' "${a[@]?m}"; echo
printf '<%s>' "${a[@]@k}"; echo
printf '<%s>' "${a[@]~}"; echo
a=("" "")
printf '<%s>' "${a[@]:-d}"; echo
printf '<%s>' "${a[*]:-d}"; echo
set -- p "q r"
printf '<%s>' "${@@K}"; echo

echo "== elements"
a=("x y" 'b$c')
declare -A m=([k]="v w")
for op in Q E A K k a u U L; do
  eval 'printf "%s <%s> <%s> <%s> <%s>\n" "$op" "${a[0]@'"$op"'}" \
    "${a[1]@'"$op"'}" "${a[7]@'"$op"'}" "${m[k]@'"$op"'}"'
done
x=${a[0]@Q}; echo "<$x> <${a[1]~}> <${a[1]~~}> <${m[k]~}>"
[[ ${m[k]@U} == "V W" ]] && echo "element in [[ ]]"

echo "== errors"
unset a
(x=${a[@]:=x}; echo "not reached $x")
echo "assign status $?"
x=${a[*]:=x}; echo "not reached $x"
echo "same line status $?"
(x=${a[*]:?no elements}; echo "not reached $x")
echo "error status $?"
(set --; [[ ${@:?no parameters} ]]; echo "not reached")
echo "positional error status $?"
