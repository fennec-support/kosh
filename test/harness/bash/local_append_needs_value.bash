#!/bin/bash
# The append suffix of local takes effect only with a value, so a bare name+
# is an invalid identifier, while name+=value appends.
f() {
  local plain+ 2> /dev/null
  echo "bare=$? ${plain-unset}"
  local joined=a
  local joined+=b
  echo "joined=$joined"
}
f
