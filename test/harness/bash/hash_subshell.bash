#!/bin/bash
# A command location remembered with hash -p resolves in every child: a
# background subshell, a pipeline stage, and a substitution. Forgetting it
# with hash -r leaves the name unknown to later children. The name is read
# from a variable so analysis does not report it as unresolved.

name=remembered
hash -p "$(type -P printf)" "$name"
"$name" 'parent %s\n' found
( "$name" 'background %s\n' found ) &
wait "$!"
echo "background-status=$?"
echo x | ( cat >/dev/null; "$name" 'pipe %s\n' found )
echo "substitution=$("$name" '%s' found)"
hash -r
( "$name" 'forgotten %s\n' found ) 2>/dev/null &
wait "$!"
echo "forgotten-status=$?"
