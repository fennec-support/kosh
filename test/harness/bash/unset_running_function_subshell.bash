#!/bin/bash
# A function that unsets itself while it runs keeps its call frame in every
# later child: a pipeline stage, a substitution, and a background subshell all
# still see it in FUNCNAME and run normally.

f() {
  unset -f f
  echo x | ( read -r v; echo "pipe=${FUNCNAME[0]} $v" )
  echo "pipe-status=$?"
  out=$(echo "subst=${FUNCNAME[0]}")
  echo "$out status=$?"
  ( echo "background=${FUNCNAME[0]}" ) &
  wait $!
  echo "background-status=$?"
}
f
