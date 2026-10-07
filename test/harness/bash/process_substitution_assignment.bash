#!/bin/bash
# A process substitution inside an assignment value or after other bytes of a
# word expands to its path, checked against bash. The path stays readable for
# the command that expands it and closes when that command finishes.
p=<(echo hi)
[[ $p == /dev/fd/* ]] && echo "input-path"
p=>(cat)
[[ $p == /dev/fd/* ]] && echo "output-path"
p=x<(echo a)y<(echo b)z
[[ $p == x/dev/fd/*y/dev/fd/*z ]] && echo "concatenated"
p=pre; p+=<(echo hi)
[[ $p == pre/dev/fd/* ]] && echo "appended"
export e=<(echo hi)
[[ $e == /dev/fd/* ]] && echo "export"
declare d=<(echo hi)
[[ $d == /dev/fd/* ]] && echo "declare"
readonly r=<(echo hi)
[[ $r == /dev/fd/* ]] && echo "readonly"
f() {
  local l=<(echo hi)
  [[ $l == /dev/fd/* ]] && echo "local"
}
f
arr=(<(echo a) x<(echo b) [4]=<(echo c))
[[ ${arr[0]} == /dev/fd/* && ${arr[1]} == x/dev/fd/* ]] && echo "array-literal"
[[ ${arr[4]} == /dev/fd/* ]] && echo "array-keyed-element"
arr[7]=<(echo d)
[[ ${arr[7]} == /dev/fd/* ]] && echo "array-element"

reader() { cat "$src"; }
src=<(echo prefix-read) reader
echo "prefix-status=$?"
src=<(echo eval-read) eval 'cat "$src"'
src=<(echo hi) :
echo "prefix-gone=${src-unset}"

q=<(echo late)
cat "$q" 2>/dev/null
echo "next-command-status=$?"
w=<(echo hi) && cat "$w" 2>/dev/null
echo "and-list-status=$?"

echo a<(echo hi)b | grep -c '^a/dev/fd/[0-9]*b$'
[[ <(echo) == /dev/fd/* ]] && echo "conditional"
case <(echo) in /dev/fd/*) echo "case-word" ;; *) echo "case-literal" ;; esac
grep -c '^/dev/fd/' <<< <(echo hi)

s="<(echo hi)"
echo "double-quoted=$s"
s='<(echo hi)'
echo "single-quoted=$s"
