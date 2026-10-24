#!/bin/bash
# Field splitting applies only to the results of expansions. Literal text
# from the source stays one word even when it holds IFS bytes, and a literal
# glued to an expansion splits only inside the expanded part.
IFS=-
echo a-b c-d
set -- a-b --
echo "n=$#"
x=p-q
printf '[%s]\n' $x a-b
printf '[%s]\n' lead-$x-tail
IFS=' '
echo a-b done

# A prefix assignment on a pipeline stage reaches that stage's environment, the
# plain, the append, and the integer-append forms, and does not persist after.
x=zz env | grep -c "^x=zz"
echo "x_after=[$x]"
export y=ab
y+=cd env | grep "^y="
echo "y_after=$y"
declare -ix w=5
w+=3 env | grep "^w="
echo "w_after=$w"
