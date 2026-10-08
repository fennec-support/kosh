#!/bin/bash

# A return inside a ${ ...; } body ends the body at the top level, with the
# status it names, and the script goes on.

echo "a${ return 3; }b"; echo "status=$?"
x=${ return 3; }; echo "status=$?"
y=${ echo one; return 4; echo two; }; echo "<$y> status=$?"
echo "after"
f() { local v=${ return 4; }; echo "in f status=$?"; }
f
