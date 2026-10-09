#!/bin/bash
# A word of an array literal that expansion turns into [index]=value is an
# ordinary element, and only a literal [index]= word sets an index, checked
# against bash.
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
arr=(a 'b c')
captured=(${arr[@]@A})
printf '<%s>' "${captured[@]}"
echo
