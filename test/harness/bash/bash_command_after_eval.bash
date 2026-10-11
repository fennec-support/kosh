#!/bin/bash

trap 'printf "[%s]\n" "$BASH_COMMAND"' EXIT

g() {
  eval 'return 3'
}

g
echo "g=$?"
echo "$(eval 'printf "inner:%s" "$BASH_COMMAND"')"
eval 'eval "y=2"'
eval 'true; [[ -n x ]] && long_name_assignment_value=1'
