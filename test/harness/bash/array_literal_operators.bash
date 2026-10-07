#!/bin/bash
# An array literal holds words. A nested parenthesis, a control operator, or a
# redirection operator inside it is a syntax error, checked against bash. Bash
# returns 1 for this error and Kosh returns 2, so only rejection is compared.
for source in 'arr=(a (b))' 'arr=(a ; b)' 'arr=(a | b)' 'arr=(a && b)' \
  'arr=(a &)' 'arr=(a ;; b)' 'arr=(x > /dev/null)' 'arr=(a >& b)' \
  'arr=(a <<< b)' 'arr=([0]=a ( c ))' 'declare -a d=(a (b))' \
  'f() { local l=(a (b)); }'; do
  if eval "$source" 2>/dev/null; then
    echo "accepted: $source"
  else
    echo "rejected: $source"
  fi
done

words=(! x = == != + - '*' / % ^ if then fi time do done)
echo "${#words[@]} ${words[*]}"
lines=(a
  b # a comment
)
declare -p lines
keyed=([1]=a [3]=b)
declare -p keyed
