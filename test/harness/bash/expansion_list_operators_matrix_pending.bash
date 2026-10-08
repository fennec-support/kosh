#!/bin/bash
# The cases of expansion_list_operators_matrix.bash that Kosh does not yet
# expand as bash does. The matrix skips these keys, and this fixture
# runs only them. Remove a key once its case agrees with bash. Bash leaks
# its internal escape byte before each space that a trimmed element
# brings into an unquoted result under an empty IFS, and Kosh does not.
pending_cases=(
  'spaces|#?|@|0|ifs_empty'
  'spaces|#?|@|2|ifs_empty'
  'spaces|#?|@|4|ifs_empty'
  'spaces|#?|@|6|ifs_empty'
  'spaces|#?|@|7|ifs_empty'
  'spaces|#?|@|9|ifs_empty'
  'spaces|#?|@|13|ifs_empty'
  'spaces|#?|*|0|ifs_empty'
  'spaces|#?|*|4|ifs_empty'
  'spaces|#?|*|6|ifs_empty'
  'spaces|#?|*|7|ifs_empty'
)
[ -n "${list_only-}" ] && return 0
is_pending_run=1
. "${BASH_SOURCE%/*}/expansion_list_operators_matrix.bash"
