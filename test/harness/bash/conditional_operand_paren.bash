#!/bin/bash

# A parenthesis where a binary operator needs an operand is a syntax error of
# the [[ ]] with status 2, and the script ends there in bash.

echo before
[[ 1 -lt (2) ]]
