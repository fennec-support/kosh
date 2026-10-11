#!/bin/bash

trap 'printf "[%s]\n" "$BASH_COMMAND"' EXIT

g() {
  eval 'return 3'
}

g
echo "g=$?"
echo "$(eval 'printf "inner:%s" "$BASH_COMMAND"')"
eval 'eval "y=2"'
echo "a$(true)[$BASH_COMMAND]"
x=$(false; true)
printf '%s\n' "after=[$BASH_COMMAND]"
cat <(true) > /dev/null; printf '%s\n' "procsub=[$BASH_COMMAND]"
z=${ true; }; printf '%s\n' "funsub=[$BASH_COMMAND]"
echo "nested $(echo "$(true)")[$BASH_COMMAND]"
eval 'true; [[ -n x ]] && long_name_assignment_value=1'
