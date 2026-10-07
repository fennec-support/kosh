#!/bin/bash
# The obsolete $[ ] spelling is arithmetic expansion. It nests, balances its
# brackets, removes double quotes in its text, expands inside double quotes,
# here-documents, and parameter words, and fails like $(( )).
echo $[1+2] $[ 2 * 3 ] "$[4+5]" '$[1]' \$[1]
echo $[ $[1+1] * 3 ] $[ $((2)) + 1 ] $(( $[3] + 1 ))
x=5
echo $[x*2] $[ "1" + 2 ] $[ (1 + 2) * 3 ] $[ 1 ? 2 : 3 ] $[ 2**10 ] $[ -1 ]
echo "a$[1]b" a$[1]b $[ 1 ]$[2] "$[ 3 ]]"
echo $[ 1 + `echo 2` ] $[ $(echo 4) ] $[ ${#x} + 1 ]
echo $[1
+ 2]
echo "[$[]]"
a=(5 6 7)
c=(1)
echo $[ a[1] ] $[ a[c[0]] + 1 ] "${a[$[1]]}" "${a[c[0]]}" "${#a[$[2]]}"
b[$[1+1]]=x
echo "${b[2]}" "${b[$[1+1]]:-unset}"
y=$[ 7 / 2 ]
echo "$y"
[[ $[2] -eq 2 ]] && echo conditional
case 3 in $[1+2]) echo pattern ;; esac
(( $[3] == 3 )) && echo command
for i in $[1] $[2]; do printf '%s ' "$i"; done
echo
echo ${unset_name:-$[7]} "${x:+$[8]}"
cat <<EOF
here $[1+1]
EOF
cat <<'EOF'
quoted $[1+1]
EOF
f() { echo $[ $1 * 2 ]; }
f 21
declare -f f
eval 'echo $[5]'
z=$(echo $[ 6 ])
echo "$z"

echo $[ 1 + ]; echo same
echo "error=$?"
echo $[ '3' ]; echo same
echo "single-quote=$?"
echo $[ ( 1 ]; echo same
echo "paren=$?"
v=$[ 1 + ] echo prefix; echo same
echo "prefix=$?"
( echo $[ 1 / 0 ]; echo after ); echo "subshell=$?"
( set -o posix; echo $[ 1 + ]; echo same ); echo "posix=$?"
echo $[ "3" ] "$[ 4 ]"
echo "quoted $[ "3" ]"; echo same
echo "quoted=$?"
echo "word" "$[ "1" ] tail"; echo same
echo "word=$?"
echo end
