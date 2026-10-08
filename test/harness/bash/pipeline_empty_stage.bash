#!/bin/bash

# A pipeline stage that expands to no command runs as an empty command. Its
# assignments stay in the stage, its redirections apply, and it returns 0. A
# stage of bare assignments expands its values in the stage, so a command
# substitution runs for its output, side effects, and status.
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

p=$(echo side >&2; exit 3) | cat
echo "substitution-statuses=${PIPESTATUS[*]}"
: | p=$(exit 4)
echo "substitution-last=$? p=${p-unset}"
/bin/rm -f "$out"
p=$(: >"$out") | cat
[ -e "$out" ] && echo made-in-stage
x=0
p=$((1/x)) | cat
echo "arithmetic=$?"

set -o pipefail
p=4 | false
echo "pipefail-first=$?"
false | p=5
echo "pipefail-last=$?"
p=$(exit 6) | cat
echo "pipefail-substitution=$?"
q=1 p=$(exit 7) | cat
echo "pipefail-two-assignments=$?"
