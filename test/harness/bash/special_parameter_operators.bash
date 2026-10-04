#!/bin/bash

# Parameter expansion operators apply to the special parameters, including a
# bang parameter that follows an operator character and must not be read as
# indirection. Each form is evaluated before and after a background job sets
# the last background process id.

set -- one two three

show_forms() {
  echo "test:+ [${!:+set}] [${$:+set}] [${?:+set}] [${#:+set}] [${-:+set}] [${0:+set}] [${_:+set}]"
  echo "test:- [${!:-fallback}] [${?:-fallback}] [${#:-fallback}]"
  echo "test- [${?-fallback}] [${#-fallback}]"
  echo "test+ [${?+set}] [${#+set}]"
  echo "length [${#?}] [${##}] [${#@}] [${#*}]"
  echo "star [${*:+set}] [${@:+set}] [${*:-fallback}]"
}

classify() {
  case $1 in
    '') echo empty ;;
    *[!0-9]*) echo mixed ;;
    *) echo digits ;;
  esac
}

echo before-job
show_forms
echo "bang-default $(classify "${!:-}")"
echo "bang-length ${#!}"

true &
wait

echo after-job
bang_plus=${!:+set}
bang_default=${!:-fallback}
bang_plain=${!-fallback}
bang_alt=${!+set}
echo "plus=$bang_plus default=$(classify "$bang_default") plain=$(classify "$bang_plain") alt=$bang_alt"
[ "$bang_default" = "$!" ] && echo default-is-pid
[ "$bang_plain" = "$!" ] && echo plain-is-pid
[ "${#!}" = "${#bang_default}" ] && echo length-matches
sliced=${!:0:1}
[ "$sliced" = "${bang_default:0:1}" ] && echo slice-matches
trimmed=${!%%[0-9]*}
echo "trimmed=[$trimmed]"
replaced=${!/[0-9]/#}
case $replaced in
  '#'*) echo replaced-first ;;
esac

echo indirection-still-works
target=second
ref=target
echo "indirect=${!ref}"
echo "indirect-default=${!ref:-fallback}"
echo "last=${!#}"
echo "positional-indirect=${!1:-none}"

echo special-done
