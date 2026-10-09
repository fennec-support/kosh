#!/bin/bash
# The prompt escapes \s, \v, \V, \!, \#, and \l expanded by ${x@P}, checked
# against bash: the shell name, the version, the history number, the command
# number across the ways a shell takes commands, and the terminal device.
export LC_ALL=C
cd "$(mktemp -d)" || exit 1

expand() {
  local template=$1
  printf '%s' "${template@P}"
}

echo "== shell name"
if [[ $(expand '\s') == "${BASH##*/}" ]]; then
  echo "shell name is the invocation name"
fi
if [[ $(expand '\s') != "${0##*/}" || ${0##*/} == "${BASH##*/}" ]]; then
  echo "shell name is not the script"
fi

echo "== version"
echo "v=[$(expand '\v')] V=[$(expand '\V')]"
echo "info=${BASH_VERSINFO[0]}.${BASH_VERSINFO[1]}.${BASH_VERSINFO[2]}"

echo "== history number"
echo "history=[$(expand '\!')]"
echo "doubled=[$(expand '\!\!')]"

echo "== terminal"
if [[ -t 0 ]]; then
  echo "stdin is a terminal"
else
  echo "tty=[$(expand '\l')]"
fi

echo "== command number"
x='\#'
echo "one [${x@P}]"
echo "two [${x@P}]"; echo "same line [${x@P}]"
echo "three [${x@P}]"

if true; then
  echo "compound [${x@P}]"
  echo "inside [${x@P}]"
fi

for i in 1 2; do
  echo "loop $i [${x@P}]"
done

f() {
  echo "function [${x@P}]"
}
f
f

echo a &
wait
(
  echo "subshell [${x@P}]"
)
echo "after subshell [${x@P}]"

eval 'echo "eval one [${x@P}]"
echo "eval two [${x@P}]"'

echo 'echo "sourced [${x@P}]"' > source_probe.sh
. ./source_probe.sh
echo "after source [${x@P}]"

echo "pipeline [$(echo "${x@P}")]"
true && echo "and list [${x@P}]"

# comment line
echo "after comment [${x@P}]"

echo "== a command string keeps one"
"$BASH" -c 'x="\\#"; echo "[${x@P}]"; true; echo "[${x@P}]"'

echo "== a script from standard input counts lines"
printf '%s\n' 'x="\\#"' 'echo "[${x@P}]"' 'true' 'echo "[${x@P}]"' | "$BASH"
