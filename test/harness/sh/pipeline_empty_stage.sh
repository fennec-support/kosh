# A pipeline stage that expands to no command runs as an empty command and
# returns 0.
p=1 | cat
echo "assignment=$? p=$p"
echo in | p=2 | cat
echo "middle=$?"
empty=
$empty | echo right
echo "expanded-empty=$?"
