#!/bin/bash

# LINENO, FUNCNAME, BASH_SOURCE, and BASH_LINENO inside functions defined in a
# sourced file report the sourced file and its own lines, and name the call
# sites in the sourcing file. FUNCNAME and BASH_SOURCE return to the sourcing
# script's own frame once a sourced file returns. The values are compared with
# Bash.

. "${BASH_SOURCE[0]%/*}/goldens/sourced_frames_inner.bash"

echo "main line $LINENO"
inner_function

wrapper()
{
  echo "wrapper line $LINENO"
  inner_function
}
wrapper

echo "again line $LINENO"
( inner_function )
echo "substitution: $(inner_function | head -n 1)"

. "${BASH_SOURCE[0]%/*}/goldens/sourced_frames_inner.bash" argument
echo "after second source $LINENO"
echo "after sources func=[${FUNCNAME[*]}] source=[${BASH_SOURCE[*]##*/}]"

source_inside()
{
  . "${BASH_SOURCE[0]%/*}/goldens/sourced_frames_inner.bash"
}
source_inside

trap 'frames trap' EXIT
echo done
