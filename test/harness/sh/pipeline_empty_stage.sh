# A pipeline stage that expands to no command runs as an empty command and
# returns 0. A stage of bare assignments expands its values in the stage, so a
# command substitution runs for its output, side effects, and status.
p=1 | cat
echo "assignment=$? p=$p"
echo in | p=2 | cat
echo "middle=$?"
empty=
$empty | echo right
echo "expanded-empty=$?"
p=$(echo side >&2; exit 3) | cat
echo "substitution-first=$?"
: | p=$(exit 4)
echo "substitution-last=$? p=${p-unset}"
made=$(mktemp -u)
p=$(touch "$made") | cat
[ -e "$made" ] && echo made-in-stage
rm -f "$made"
x=0
p=$((1/x)) | cat
echo "arithmetic=$?"
