#!/bin/bash

# An eval adds no BASH_SOURCE, FUNCNAME, or BASH_LINENO row, at the top level,
# inside a function, and inside a sourced file. The values are compared with
# Bash. Functions defined by eval or source and repeated command substitutions
# also match Bash.

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

eval 'eval_defined_function() { printf "eval-function=%s\\n" "$1"; }'
eval_defined_function alive
source_file=$(mktemp)
trap '[ -n "$source_file" ] && /bin/rm -f "$source_file"' EXIT
printf 'source_defined_function() { printf "source-function=%%s\\n" "$1"; }\n' \
    > "$source_file"
source "$source_file"
/bin/rm -f "$source_file"
source_file=
source_defined_function alive
substitution_source='printf substitution'
printf 'substitution-reparse=%s,%s\n' "$($substitution_source)" \
    "$($substitution_source)"

positional_parameter_substitution() {
    printf 'positional-before=%s\n' "$1"
    nested_value=$(true)
    printf 'positional-after=%s\n' "$1"
}
positional_parameter_substitution alive

persistent_substitution_cache() {
    printf 'persistent-cache=%s\n' "$(printf alive)"
}
persistent_substitution_cache
persistent_substitution_cache
