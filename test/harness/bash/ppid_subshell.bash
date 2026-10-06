#!/bin/bash
# PPID names the parent of the original shell in every substitution, subshell,
# and pipeline stage, while BASHPID differs in each of them.
parent=$PPID
[ "$parent" -gt 0 ] && echo "ppid positive"
a=$(echo "$PPID")
b=`echo "$PPID"`
c=$( (echo "$PPID") )
d=$(echo x | { cat > /dev/null; echo "$PPID"; })
[ "$a" = "$parent" ] && echo "substitution matches"
[ "$b" = "$parent" ] && echo "backticks match"
[ "$c" = "$parent" ] && echo "nested subshell matches"
[ "$d" = "$parent" ] && echo "pipeline stage matches"
( [ "$PPID" = "$parent" ] && echo "subshell matches" )
echo x | { cat > /dev/null; [ "$PPID" = "$parent" ] && echo "pipe stage matches"; }
f() { echo "$PPID"; }
g=$(f)
[ "$g" = "$parent" ] && echo "function substitution matches"
[ "$(echo "$BASHPID")" != "$BASHPID" ] && echo "bashpid differs in substitution"
[ "$(echo "$SRANDOM")" -ge 0 ] && echo "srandom readable"
