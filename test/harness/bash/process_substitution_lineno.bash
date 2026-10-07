#!/bin/bash

# A process substitution reports the line of each command in its body, as a
# command substitution does, instead of counting from the start of the body.
:
cat <(echo "input at $LINENO")
cat <(echo "first at $LINENO"
  echo "second at $LINENO"; echo "same at $LINENO")
while read -r line; do echo "$line"; done < <(echo "redirected at $LINENO")
cat <(cat <(echo "nested at $LINENO"))

report() {
  cat <(echo "function at $LINENO")
}
report

collected=$(echo "output at $LINENO" > >(cat); wait)
echo "$collected"

# A function defined inside a process substitution or an eval counts its lines
# from the file even while the text that defined it is still running.
cat <(
  defined_inside() {
    echo "defined inside at $LINENO"
  }
  defined_inside
)
eval 'evaluated() {
  echo "evaluated at $LINENO"
}
evaluated'
evaluated
