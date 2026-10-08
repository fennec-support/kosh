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
  'body|${ echo "$unset_name"; }||0|nounset'
  'body|${ echo "$unset_name"; }||1|nounset'
  'body|${ echo "$unset_name"; }||2|nounset'
  'body|${| REPLY=$unset_name; }||0|nounset'
  'body|${| REPLY=$unset_name; }||1|nounset'
  'body|${| REPLY=$unset_name; }||2|nounset'
)
[ -n "${list_only-}" ] && return 0
is_pending_run=1
. "${BASH_SOURCE%/*}/funsub_contexts_matrix.bash"
