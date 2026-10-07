#!/bin/bash
# An opening parenthesis glued to a nonempty assignment value is a syntax error,
# including an escaped or quoted < or > before it, while NAME=( and NAME+=(
# open an array and an escaped parenthesis stays literal, checked against bash.
# Kosh reports a syntax error with a different status, so only rejection is
# compared.
for source in 'p=\<(x)' 'p=\>(x)' 'p="<"(x)' "p='<'(x)" 'p=a(x)' \
  'p=$(echo)(x)' 'p+=a(x)' 'p=\<(x) echo hi' 'export p=\<(x)' \
  'declare p=a(x)'; do
  if eval "$source" 2>/dev/null; then
    echo "accepted: $source"
  else
    echo "rejected: $source"
  fi
done
p=(x)
declare -p p
p+=(y)
declare -p p
q=\<\(x\)
declare -p q
shopt -s extglob
echo @(a)<(echo x) | sed 's#/dev/fd/[0-9]*#<path>#'
