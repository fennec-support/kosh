#!/bin/bash
# A [[ ]] operand error fails the conditional with status 2 for a non-integer
# -t and continues, and an arithmetic syntax error in a comparison fails the
# command without aborting, the lines cond.tests runs. A unary operator
# without its operand also fails with status 2, and a chain of 30000 &&
# operands evaluates.
[[ -t X ]] 2>/dev/null
echo "tty_operand=$?"
[[ 7 -eq 4+ ]] 2>/dev/null
echo "arith_operand=$?"
[[ X -eq 4 ]] 2>/dev/null
echo "name_operand=$?"
for malformed in '[[ -n ]]' '[[ ! -n ]]' '[[ ( -n ) ]]' '[[ -n < a ]]'; do
  eval "$malformed" 2>/dev/null
  echo "$malformed=$?"
done
chain='[[ a'
i=0
while (( i < 30000 )); do
  chain+=' && a'
  (( i++ ))
done
eval "$chain || \"\" ]]"
echo "long_chain=$?"
echo survived
