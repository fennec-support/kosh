#!/bin/bash
# The cases of nameref_contexts_matrix.bash that Kosh does not yet
# expand as bash does. The matrix skips these keys, and this fixture
# runs only them. Remove a key once its case agrees with bash.
# Two groups remain. An export through a reference to an array: bash
# keeps the array and prints it with the export attribute, as in
# declare -ax, while Kosh assigns element 0 or key 0 and records no
# export attribute for an array. Every scalar write through a reference
# to an associative array: both shells set key 0, and the cases differ
# only in the order declare -p lists the two keys, which follows bash's
# hash table in one shell and Kosh's own table in the other.
pending_cases=(
  'array|export ref=exported||0|write'
  'array|export ref=exported||1|write'
  'array|export ref=exported||2|write'
  'array|export ref=exported||3|write'
  'assoc|ref=new||0|write'
  'assoc|ref=new||1|write'
  'assoc|ref=new||2|write'
  'assoc|ref=new||3|write'
  'assoc|ref+=more||0|write'
  'assoc|ref+=more||1|write'
  'assoc|ref+=more||2|write'
  'assoc|ref+=more||3|write'
  'assoc|ref[1]=elem||0|write'
  'assoc|ref[1]=elem||1|write'
  'assoc|ref[1]=elem||2|write'
  'assoc|ref[1]=elem||3|write'
  'assoc|ref[2]=k||0|write'
  'assoc|ref[2]=k||1|write'
  'assoc|ref[2]=k||2|write'
  'assoc|ref[2]=k||3|write'
  'assoc|((ref = 4 * 2))||0|write'
  'assoc|((ref = 4 * 2))||1|write'
  'assoc|((ref = 4 * 2))||2|write'
  'assoc|((ref = 4 * 2))||3|write'
  'assoc|((ref++))||0|write'
  'assoc|((ref++))||1|write'
  'assoc|((ref++))||2|write'
  'assoc|((ref++))||3|write'
  'assoc|let ref+=3||0|write'
  'assoc|let ref+=3||1|write'
  'assoc|let ref+=3||2|write'
  'assoc|let ref+=3||3|write'
  'assoc|read -r ref <<< rd||0|write'
  'assoc|read -r ref <<< rd||1|write'
  'assoc|read -r ref <<< rd||2|write'
  'assoc|read -r ref <<< rd||3|write'
  'assoc|printf -v ref %s pv||0|write'
  'assoc|printf -v ref %s pv||1|write'
  'assoc|printf -v ref %s pv||2|write'
  'assoc|printf -v ref %s pv||3|write'
  'assoc|: ${ref:=assigned}||0|write'
  'assoc|: ${ref:=assigned}||1|write'
  'assoc|: ${ref:=assigned}||2|write'
  'assoc|: ${ref:=assigned}||3|write'
  'assoc|export ref=exported||0|write'
  'assoc|export ref=exported||1|write'
  'assoc|export ref=exported||2|write'
  'assoc|export ref=exported||3|write'
  'assoc|OPTIND=1; getopts ab ref -b||0|write'
  'assoc|OPTIND=1; getopts ab ref -b||1|write'
  'assoc|OPTIND=1; getopts ab ref -b||2|write'
  'assoc|OPTIND=1; getopts ab ref -b||3|write'
  'assoc|f() { local -n lr=ref; lr=via_local; }; f||0|write'
  'assoc|f() { local -n lr=ref; lr=via_local; }; f||1|write'
  'assoc|f() { local -n lr=ref; lr=via_local; }; f||2|write'
  'assoc|f() { local -n lr=ref; lr=via_local; }; f||3|write'
  'assoc|f() { local -n lr=target; lr=via_name; }; f||0|write'
  'assoc|f() { local -n lr=target; lr=via_name; }; f||1|write'
  'assoc|f() { local -n lr=target; lr=via_name; }; f||2|write'
  'assoc|f() { local -n lr=target; lr=via_name; }; f||3|write'
)
[ -n "${list_only-}" ] && return 0
is_pending_run=1
. "${BASH_SOURCE%/*}/nameref_contexts_matrix.bash"
