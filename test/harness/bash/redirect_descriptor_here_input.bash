#!/bin/bash
# A numbered or {name} descriptor takes a here-string or a here-document the
# way bash does, on exec, a command, a function, a group, and a subshell, and
# a {name} allocation binds the name it gives.
echo "== numbered here-string on exec =="
exec 7<<< "seven"
/bin/cat <&7
exec 7<&-

echo "== numbered here-string on a command =="
/bin/cat 3<<< "three" <&3
echo "rc=$?"

echo "== numbered here-string on a function and a group =="
f() { /bin/cat <&5; }
f 5<<< "five"
{ /bin/cat <&6; } 6<<< "six"
( /bin/cat <&4 ) 4<<< "four"

echo "== allocated here-string =="
exec {hs}<<< "allocated string"
if [ "$hs" -ge 10 ]; then echo "hs=high"; else echo "hs=$hs"; fi
/bin/cat <&"$hs"
exec {hs}<&-
echo "closed=$?"

echo "== allocated here-document =="
exec {hd}<<EOF
allocated $((1 + 1)) document
EOF
if [ "$hd" -ge 10 ]; then echo "hd=high"; else echo "hd=$hd"; fi
/bin/cat <&"$hd"
exec {hd}<&-

echo "== allocated here-document into an array element =="
declare -a slots
exec {slots[2]}<<'EOF'
literal $x
EOF
/bin/cat <&"${slots[2]}"
exec {slots[2]}<&-

echo "== numbered here-document on exec =="
exec 8<<EOF
eight
EOF
/bin/cat <&8
exec 8<&-

echo done
