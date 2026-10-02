#!/bin/bash

# A function body reports the line it is written on inside its own defining
# file. The call site reports the line of the call. Each pair is printed
# together, and no body line matches the call line that reached it.

. "${BASH_SOURCE[0]%/*}/goldens/function_lineno_inner.bash"

echo sourced-definitions
echo "call at $LINENO"
first_line_function
echo "call at $LINENO"
later_function

echo local-definition
local_function() {
  echo "local at $LINENO"
}

echo "call at $LINENO"
local_function

echo nested-call
outer_function() {
  echo "outer at $LINENO"
  local_function
}

echo "call at $LINENO"
outer_function

echo redefined-function
for pass in 1 2 3; do
  redefined_function() { echo "pass $pass at $LINENO"; }
  redefined_function
  redefined_function() {
echo "pass $pass at $LINENO"; }
  redefined_function
done
echo "call at $LINENO"

echo function-lineno-done

echo streamed-units
unit_function() { echo "unit body at $LINENO"; }
echo "unit one at $LINENO"
unit_function
if true; then
  echo "unit two at $LINENO"
  unit_function
fi
echo "unit three at $LINENO"; unit_function
echo "unit four at $LINENO"

echo substitution-lineno
echo "plain $(echo $LINENO) backquote `echo $LINENO`"
substitution_function() {
  echo "body $(echo $LINENO) frames $(echo "${FUNCNAME[*]}|${BASH_SOURCE[*]##*/}")"
  echo "nested $(echo "$(echo $LINENO)")"
}
substitution_function
captured=$(substitution_function)
echo "$captured"
