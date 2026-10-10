#!/bin/bash
# An array literal element that starts with a subscript keeps the blanks up
# to its closing bracket in one word, as in bash, so the key or index holds
# them instead of the element splitting apart.
declare -A map=([b c]=2)
echo "map=${map[b c]} count=${#map[@]}"
indexed=([1 + 1]=x)
declare -p indexed
plain=([x y] [x y]z=1)
declare -p plain
declare -A quoted
quoted=(["x y"]=1)
echo "quoted=${quoted[x y]}"
