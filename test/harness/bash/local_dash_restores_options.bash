#!/bin/bash
# local - saves the set options, and the function return restores them; a
# change without local - stays after the return.
restore_target() {
  local -
  set -f
  set -o noclobber
  case $- in *f*) echo "inside noglob=on" ;; esac
}
leak_target() {
  set -f
}
nested_target() {
  local - value=1
  set -u
  restore_target
  case $- in *f*) echo "nested noglob=leaked" ;; *) echo "nested noglob=off" ;; esac
  echo "nested value=$value"
}

restore_target
case $- in *f*) echo "after restore noglob=on" ;; *) echo "after restore noglob=off" ;; esac
[ -o noclobber ] && echo "noclobber=on" || echo "noclobber=off"
nested_target
case $- in *u*) echo "nounset=on" ;; *) echo "nounset=off" ;; esac
leak_target
case $- in *f*) echo "after leak noglob=on" ;; *) echo "after leak noglob=off" ;; esac
set +f
