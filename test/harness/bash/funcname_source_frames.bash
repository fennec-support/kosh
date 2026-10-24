#!/bin/bash
# The BASH_LINENO call line of a function reached through another file, and the
# frames, line numbers, and sources that a function and a trap defined by a
# nested sourced file report after many sources.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT

printf '%s\n' \
  'lines() {' \
  '  echo "LINE=[${BASH_LINENO[*]}]"' \
  '}' > "$dir/lib.sh"

printf '%s\n' 'nested' > "$dir/mid.sh"

echo "--- call lines ---"
source "$dir/lib.sh"

nested() {
  lines
}

carrier() {
  source "$dir/mid.sh"
}

carrier

# A function and a trap defined by a file sourced from another sourced file
# still report the same frames, line numbers, and sources after the outer file
# sources a leaf file many times.
echo "--- many nested sources ---"
printf '%s\n' \
  'defined() {' \
  '  echo "FUNCNAME=[${FUNCNAME[*]}] LINENO=$LINENO SOURCE=[${BASH_SOURCE[*]##*/}]"' \
  '  read -r line name file <<< "$(caller 0)"' \
  '  echo "caller=$line $name ${file##*/}"' \
  '}' \
  "trap 'echo trap_fired \${BASH_SOURCE[0]##*/} \$LINENO' USR1" > "$dir/def.sh"

printf '%s\n' \
  'if [ "$1" = 1 ] || [ "$1" = 300 ]; then' \
  '  echo "leaf $1 LINENO=$LINENO SOURCE=${BASH_SOURCE[0]##*/}"' \
  'fi' > "$dir/leaf.sh"

printf '%s\n' \
  'source "$dir/def.sh"' \
  'count=1' \
  'while [ "$count" -le 300 ]; do' \
  '  source "$dir/leaf.sh" "$count"' \
  '  count=$((count + 1))' \
  'done' \
  'defined' > "$dir/driver.sh"

source "$dir/driver.sh"
defined
kill -USR1 $$
echo "after trap"
