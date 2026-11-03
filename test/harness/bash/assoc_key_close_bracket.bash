#!/bin/bash

# A quoted or backslash-escaped ] in an associative key neither closes the
# subscript nor ends the assignment target, in a list, an element assignment,
# and an expansion.

dump() {
  local -n map=$1
  local key
  for key in "${!map[@]}"; do echo "<$key>=<${map[$key]}>"; done | sort
  echo --
}

declare -A m
m=(["k]x"]=1 ['a]b']=2 [c]=3)
dump m
m['k]x']=9
m["z]=y"]=8
m['w]']+=1
m['w]']+=2
dump m
echo "${m['k]x']} ${m["z]=y"]}"
echo ${m['k]x']} ${m["z]=y"]-none} ${m['missing]']-none}
echo "${m['k]x']:-none} ${m['nope]']:-none} ${#m['k]x']}"
k='q]r'
m[$k]=5
echo "${m[$k]}"
unset "m['k]x']"
unset 'm["z]=y"]'
dump m
m+=(["p]q"]=1)
dump m
[[ -v m['a]b'] ]]; echo "set $?"
[[ -v m['a]b]x'] ]]; echo "unset $?"
m[\]=]=2
dump m

# A key expanded inside an arithmetic subscript is one literal key, whatever
# brackets, dollar signs, spaces, quotes, or backslashes it holds, for a read,
# an update, an assignment, and let.
declare -A n
for k in 'a]b' 'a[b' 'a$b' 'a b' "a'b" 'a"b' 'a\b' '$(echo hi)' ']' 'x]+h[y'; do
  n[$k]=1
  echo "$k read=$(( n[$k] ))"
  (( n[$k]++ ))
  let 'n[$k]+=1'
  (( n[$k] *= 2, n[$k] > 5 ? n[$k] : 0 ))
  echo "$k now=${n[$k]} sum=$(( n[$k] + n[$k] ))"
done
dump n
k='q]r'
(( n[$k] = 7 ))
echo "assigned=${n[$k]} quoted=$(( n["q]r"] + ${#k} ))"
declare -n ref=n
echo "through nameref=$(( ref[$k] ))"

# An arithmetic operand of [[ ]] expands its subscripts the same way, on
# either side of every integer comparison and inside a function, an eval,
# and a command substitution. A subscript expands once, so a key that holds
# a dollar sign is not expanded again.
declare -A c e
z=7
for k in 'a]b' '$z' 'a[b' "a'b" 'a"b' '$(echo hi)'; do
  c[$k]=2
  [[ e[$k] -eq 0 && c[$k] -eq 2 && 2 -eq c[$k] && c[$k]+1 -gt 2 ]] &&
    echo "$k compared"
  [[ c[$k] -ne 2 || c[$k] -lt 2 || c[$k] -le 1 || c[$k] -ge 3 ]] ||
    echo "$k ordered"
done
c[7]=9
k='$z'
[[ c[$k] -eq 9 ]] || echo "dollar key read once"
k='a]b'
[[ c["a]b"] -eq 2 && c["$k"] -eq 2 ]] && echo "quoted keys compared"
f() { [[ c[$k] -eq 2 ]] && echo "function compared"; }
f
eval '[[ c[$k] -eq 2 ]] && echo "eval compared"'
echo "$( [[ c[$k] -eq 2 ]] && echo "substitution compared" )"
i=0
[[ c[$((i++))] -eq 0 ]]
echo "expanded once i=$i"

# A single-quoted or backslash-escaped key in an arithmetic subscript is
# literal, and a double-quoted [[ ]] operand keeps its subscript whole.
c['$z']=4
(( c['a]b'] == 2 )) && echo "single quoted key in (( ))"
[[ c['a]b'] -eq 2 && c[a\]b] -eq 2 ]] && echo "single quoted key in [[ ]]"
echo "single quoted sum=$(( c['a]b'] + c['$z'] ))"
[[ "c[$k]" -eq 2 && "c[\$z]" -eq 4 ]] && echo "quoted operand compared"

# A name reference to an array takes the subscript of the target.
declare -A target=([k]=7 [ab]=8)
declare -a list=(5 6)
declare -n named=target listed=list
i=1
echo "nameref keys=$(( named[k] )) $(( named[$k] + 0 )) index=$(( listed[i] ))"
(( named[k] += 1, listed[i]++ ))
echo "nameref writes=${target[k]} ${list[1]}"

# let and other commands expand a subscript in an assignment-like argument,
# and let expands an associative subscript once more.
declare -A l
k=ab
let l[\$k]=1 "l[\$k]+=1" l['$k']+=1 l["$k"]+=1
declare -p l
echo h[\$k]=1 h["$k"]=2 h['x y']=3 h[$k]+=4
