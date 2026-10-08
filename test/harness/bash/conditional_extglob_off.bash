#!/bin/bash

# A pattern in [[ ]] honors extended groups even with extglob off.

shopt -u extglob
x=zab; [[ $x == *@(a|b)* ]]; echo "star $?"
[[ a == @(a|b) ]]; echo "one $?"
[[ ab == +(a|b) ]]; echo "plus $?"
[[ b == !(a) ]]; echo "not $?"
[[ c == @(a|b) ]]; echo "miss $?"
[[ c != @(a|b) ]]; echo "differ $?"
