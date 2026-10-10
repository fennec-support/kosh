#!/bin/bash
# pushd -n and popd -n change the stack without changing directory, cd ""
# is an error, declare -ft and -fr set an attribute without printing the
# function, and %b expands \u and \U escapes, all as in bash.
cd /
pushd -n /tmp > /dev/null
pushd -n /usr > /dev/null
echo "pushd-n=$? pwd=$PWD"
dirs
popd -n > /dev/null
echo "popd-n=$? pwd=$PWD"
dirs
cd "" 2> /dev/null
echo "cd-empty=$? pwd=$PWD"
traced() { echo traced; }
declare -ft traced
echo "trace=$?"
declare -fr traced
echo "readonly=$?"
traced() { echo redefined; } 2> /dev/null
traced
declare -ft missing_function
echo "trace-missing=$?"
printf '%b|%b\n' '☺' '\U0001F600' | od -An -tx1
