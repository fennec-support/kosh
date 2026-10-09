#!/bin/bash
# The cases of nameref_contexts_matrix.bash that Kosh does not yet
# expand as bash does. The matrix skips these keys, and this fixture
# runs only them. Remove a key once its case agrees with bash.
# One group remains. A step or a let through a reference to an
# associative array that holds no key 0: both shells write key 0 and
# print the same listing, but Kosh also warns that the unset element
# expands to empty, and bash stays silent.
pending_cases=(
  'assoc|((ref++))||0|write'
  'assoc|((ref++))||1|write'
  'assoc|((ref++))||2|write'
  'assoc|((ref++))||3|write'
  'assoc|let ref+=3||0|write'
  'assoc|let ref+=3||1|write'
  'assoc|let ref+=3||2|write'
  'assoc|let ref+=3||3|write'
)
[ -n "${list_only-}" ] && return 0
is_pending_run=1
. "${BASH_SOURCE%/*}/nameref_contexts_matrix.bash"
