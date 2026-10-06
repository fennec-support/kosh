#!/bin/bash

# LINENO and BASH_LINENO inside an eval count from the line of the eval
# command, at the top level, inside a function, inside a nested eval, and for a
# function an eval calls. The values are compared with Bash.

echo line1
eval 'echo A $LINENO
echo B $LINENO

echo C $LINENO'

show_lines()
{
  echo "show_lines ${BASH_LINENO[*]} $LINENO"
}

in_function()
{
  eval 'echo FA $LINENO
echo FB $LINENO'
  eval "show_lines"
}
in_function

eval 'echo X $LINENO; show_lines'
eval 'eval "echo Y $LINENO
echo Z $LINENO"'
eval 'in_function'

via_eval()
{
  eval 'in_function'
}
eval 'eval "via_eval"'
echo end $LINENO
