#!/bin/bash

# A syntax error in a -c string, a script, an eval, or a sourced file returns
# status 2.
"$BASH" -c 'if then fi'
echo "command-string=$?"
"$BASH" -c 'echo "${unterminated'
echo "unterminated=$?"

eval 'if then fi'
echo "eval=$?"
eval '(('
echo "eval-open=$?"

script_file=$(mktemp)
trap '[ -n "$script_file" ] && /bin/rm -f "$script_file"' EXIT
printf 'if then fi\n' > "$script_file"
"$BASH" "$script_file"
echo "script=$?"
source "$script_file"
echo "source=$?"
