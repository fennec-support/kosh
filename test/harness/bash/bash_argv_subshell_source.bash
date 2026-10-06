#!/bin/bash
# A file sourced without arguments inside a subshell of a function pushes only
# its own path onto BASH_ARGV, as in the function body itself, before any
# earlier command has read the argument arrays.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf '%s\n' 'echo "argc=${BASH_ARGC[*]} argv=${BASH_ARGV[*]##*/}"' \
  > "$dir/report.sh"

background() {
  ( . "$dir/report.sh" ) &
  wait "$!"
}
background a b

pipeline() {
  echo x | ( cat >/dev/null; . "$dir/report.sh" )
}
pipeline c d
