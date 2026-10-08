#!/bin/bash
# shellcheck disable=SC2257,SC2154,SC2034
# Bash expands the redirections of a program in a forked child, so an
# assignment, a function substitution, or an arithmetic side effect in a word
# or a here-document body is lost. A builtin, a function, a group, and exec
# expand them in the shell itself.
export LC_ALL=C
cd "$(mktemp -d)" || exit 1

show() {
  printf '%s: v=%s w=%s\n' "$1" "${v-unset}" "${w-unset}"
}

v=0
cat <<< "${ v=1; echo s; }"
show here-string-program

v=0
cat <<EOF
<${ v=1; echo s; }>
EOF
show here-document-program

v=0
cat <<< "${| v=1; REPLY=r; }"
show value-substitution-program

v=0
cat <<'EOF'
${ v=1; echo quoted; }
EOF
show quoted-here-document-program

v=0
read -r line <<< "${ v=1; echo s; }"
show here-string-builtin

v=0
while read -r line; do :; done <<EOF
${ v=1; echo s; }
EOF
show here-document-builtin

v=0
cat_function() { cat; }
cat_function <<< "${ v=1; echo s; }"
show here-string-function

v=0
{ cat; } <<< "${ v=1; echo s; }"
show here-string-group

v=0
cat > "${ v=1; echo out; }"
show output-word-program

v=0
echo hi > "${ v=1; echo out2; }"
show output-word-builtin

v=0
cat < "${ v=1; echo /dev/null; }"
show input-word-program

v=0
ls /dev/null 2> "${ v=1; echo err; }"
show error-word-program

v=0
ls /dev/null >&"${ v=1; echo 1; }"
show duplicate-word-program

v=0
cat < /dev/null > "${ v=1; echo out3; }"
show two-redirections-program

v=0
cat <<< "${ v=1; echo s; }" "${ w=2; echo /dev/null; }"
show word-and-redirection

v=0
cat_local() {
  local v=0
  cat <<< "${ v=1; echo s; }"
  echo "local=$v"
}
cat_local
show local-program

v=0
cat_local_builtin() {
  local v=0
  read -r line <<< "${ v=1; echo s; }"
  echo "local=$v"
}
cat_local_builtin
show local-builtin

v=0
cat <<< "${ false; echo s; }"
echo "status=$?"

v=0
cat <<< "a${ echo b >&2; echo c; }d" 2>&1
show nested-output-order

v=0
<<< "${ v=1; echo s; }"
show null-command

v=0
no_such_program_here <<< "${ v=1; echo s; }" 2> /dev/null
show missing-program

v=0
( cat ) <<< "${ v=1; echo s; }"
show subshell

v=0
cat <<< "${ v=1; echo s; }" | cat
show pipeline-stage

v=0
v=9 cat <<< "${ v=1; echo s; }"
show prefix-assignment-program

v=0
v=9 read -r line <<< "${ v=1; echo s; }"
show prefix-assignment-builtin

unset x
cat <<< "${x:=1}"
show "assign-default-program x=${x-unset}"

unset x
read -r line <<< "${x:=1}"
show "assign-default-builtin x=${x-unset}"

y=0
cat <<< "$((y=3))"
show "arithmetic-program y=$y"

y=0
read -r line <<< "$((y=3))"
show "arithmetic-builtin y=$y"

i=0
values=(p q r)
cat <<< "${values[i++]}"
show "subscript-program i=$i"

i=0
read -r line <<< "${values[i++]}"
show "subscript-builtin i=$i"

unset x
cat > "${x:=/dev/null}"
show "output-assign-program x=${x-unset}"

unset x
echo hi > "${x:=/dev/null}"
show "output-assign-builtin x=${x-unset}"

unset x
cat <<EOF
${x:=1}
EOF
show "here-document-assign-program x=${x-unset}"

unset x
cat <<EOF
$((n=5))
EOF
show "here-document-arithmetic-program n=${n-unset}"
