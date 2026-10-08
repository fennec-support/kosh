#!/bin/bash

# A backslash-escaped ] in an associative key neither closes the subscript nor
# ends the assignment target, and declare marks through a nameref leave a
# read-only target alone.

declare -A m
m[\]=]=2
declare -p m

readonly t=1
declare -n r=t
declare -i r
echo "integer $?"
declare -p t
declare -l r
echo "lower $?"
declare -p t
