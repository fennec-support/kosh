#!/bin/bash

# A pipeline stage that expands to no command runs as an empty command. Its
# assignments stay in the stage, its redirections apply, and it returns 0.
p=1 | cat
echo "assignment=$? p=$p"
echo in | p=2 | cat
echo "middle=$?"
empty=
$empty | echo right
echo "expanded-empty=$?"

out=$(mktemp)
trap '[ -n "$out" ] && /bin/rm -f "$out"' EXIT
/bin/rm -f "$out"
p=3 >"$out" | cat
echo "redirect=$?"
[ -e "$out" ] && echo created

set -o pipefail
p=4 | false
echo "pipefail-first=$?"
false | p=5
echo "pipefail-last=$?"
