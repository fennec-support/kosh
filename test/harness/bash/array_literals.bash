#!/bin/bash
# An array literal holds words, checked against bash. A nested parenthesis, a
# control operator, or a redirection operator inside it is a syntax error, and
# so is an opening parenthesis glued to a nonempty assignment value. Bash
# returns 1 for these errors and Kosh returns 2, so only rejection is compared.
# A word that expansion turns into [index]=value is an ordinary element, only a
# literal [index]= word sets an index, and the blanks of a literal subscript
# stay in one word.
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

text='[3]=x'
lit=(${text} [1]=y)
declare -p lit
split=($text "$text" [2]=z)
declare -p split
words='[0]="a" [1]="b c"'
fields=($words)
printf '<%s>' "${fields[@]}"
echo
declare -a decl=($text [5]=w)
declare -p decl
for round in 1 2 3; do
  again=([0]=x [5]=y [10]=z)
  declare -p again
done
arr=(a 'b c')
captured=(${arr[@]@A})
printf '<%s>' "${captured[@]}"
echo

declare -A map=([b c]=2)
echo "map=${map[b c]} count=${#map[@]}"
indexed=([1 + 1]=x)
declare -p indexed
plain=([x y] [x y]z=1)
declare -p plain
declare -A quoted
quoted=(["x y"]=1)
echo "quoted=${quoted[x y]}"

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
