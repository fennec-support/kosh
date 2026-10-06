#!/bin/bash

# An eval adds no BASH_SOURCE, FUNCNAME, or BASH_LINENO row, at the top level,
# inside a function, and inside a sourced file. The values are compared with
# Bash.

show()
{
  echo "$1: sources=${#BASH_SOURCE[@]} names=[${FUNCNAME[*]}] lines=[${BASH_LINENO[*]}]"
}

show top
eval 'show eval_top'

in_function()
{
  show function
  eval 'show eval_in_function'
  eval 'eval "show nested_eval"'
}
in_function

outer()
{
  in_function
}
outer

eval 'in_function'
eval 'eval "outer"'
