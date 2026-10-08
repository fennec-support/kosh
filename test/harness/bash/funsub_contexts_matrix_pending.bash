#!/bin/bash
# The cases of funsub_contexts_matrix.bash that Kosh does not yet
# expand as bash does. The matrix skips these keys, and this fixture
# runs only them. Remove a key once its case agrees with bash.
pending_cases=(
  'body|${ printf "it'\''s\n\n\n"; }||22|'
  'body|${ side=changed; echo s; }||9|'
  'body|${ side=changed; echo s; }||10|'
  'body|${| side=changed; REPLY=r; }||9|'
  'body|${| side=changed; REPLY=r; }||10|'
)
[ -n "${list_only-}" ] && return 0
is_pending_run=1
. "${BASH_SOURCE%/*}/funsub_contexts_matrix.bash"
