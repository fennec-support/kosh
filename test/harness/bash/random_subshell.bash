#!/bin/bash

# Every subshell reseeds RANDOM, so a substitution, a nested subshell, and a
# pipeline stage each draw numbers of their own. The parent's sequence is left
# where its seed put it, and a seed set inside a subshell repeats there. Two
# draws per subshell keep an accidental match out of reach.

RANDOM=7
first=$(echo "$RANDOM $RANDOM")
second=$(echo "$RANDOM $RANDOM")
nested=$( (echo "$RANDOM $RANDOM") )
staged=$(echo x | { cat > /dev/null; echo "$RANDOM $RANDOM"; })
parent="$RANDOM $RANDOM"

RANDOM=7
seeded="$RANDOM $RANDOM"
[ "$parent" = "$seeded" ] && echo "parent sequence is untouched"

[ "$first" != "$parent" ] && echo "substitution differs from parent"
[ "$first" != "$second" ] && echo "substitutions differ from each other"
[ "$nested" != "$parent" ] && [ "$nested" != "$first" ] &&
  echo "nested subshell differs"
[ "$staged" != "$parent" ] && [ "$staged" != "$first" ] &&
  echo "pipeline stage differs"

inner=$(RANDOM=9; echo "$RANDOM $RANDOM")
RANDOM=9
outer="$RANDOM $RANDOM"
[ "$inner" = "$outer" ] && echo "seed inside a subshell repeats"
