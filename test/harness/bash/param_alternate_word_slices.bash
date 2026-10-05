#!/bin/bash
# The word of an alternate operator expands a quoted positional or array slice
# into one field per element, and the # and ? parameters take an operator the
# way a name does, checked byte-for-byte against bash. An operator word that
# names a missing positional parameter stays silent without nounset, and an
# unused word is never evaluated.
show() {
  printf '%s:' "$#"
  for a in "$@"; do printf '[%s]' "$a"; done
  printf '\n'
}

arr=(w "x y" z)

slice_at() { x=1; show ${x+"${@:2}"}; }
slice_at_default() { show ${missing-"${@:2}"}; }
slice_at_affixed() { x=1; show ${x+pre"${@:2}"post}; }
slice_at_length() { x=1; show ${x+"${@:1:2}"}; }
slice_at_empty_length() { x=1; show ${x+"${@:2:0}"}; }
slice_at_unquoted() { x=1; show ${x+${@:2}}; }
slice_at_outer_quoted() { x=1; show "${x+"${@:2}"}"; }
slice_at_outer_mixed() { x=1; show "${x+a ${@:2} b}"; }
slice_at_after_field() { x=1; show ${x+"$@" "${@:2}"}; }
slice_array() { x=1; show ${x+"${arr[@]:1}"}; }
slice_array_length() { x=1; show ${x+"${arr[@]:1:1}"}; }
slice_array_negative() { x=1; show ${x+"${arr[@]: -2}"}; }
slice_array_unquoted() { x=1; show ${x+${arr[@]:1}}; }
slice_array_default() { show ${missing-"${arr[@]:1}"}; }
slice_array_outer_quoted() { x=1; show "${x+"${arr[@]:1}"}"; }
slice_array_affixed() { x=1; show ${x+pre"${arr[@]:1}"post}; }
slice_both() { x=1; show ${x+"${@:2}" "${arr[@]:1}"}; }
hash_plus() { show ${#+"$@"}; }
hash_colon_plus() { show ${#:+"$@"}; }
hash_outer_quoted() { show "${#+"$@"}"; }
hash_affixed() { show ${#:+pre"$@"post}; }
hash_mixed() { show "${#:+"$@" tail}"; }
hash_minus() { show ${#-"$@"}; }
hash_literal() { show ${#+a b}; }
status_plus() { show ${?+"$@"}; }
status_colon_minus() { show ${?:-"$@"}; }
unused_positional() { x=1; show ${missing+"$1" "$2"}; }
used_positional() { x=1; show ${x+"$1" "$2"}; }
used_positional_quoted() { x=1; show "${x+$1 $2}"; }
default_positional() { show ${missing-"$1" "$2"}; }
double_at() { x=1; show ${x+"$@" "$@"}; }
double_at_quoted() { x=1; show "${x+"$@" "$@"}"; }
leading_space_quoted() { x=1; show ${x+ "a b"}; }
leading_space_at() { x=1; show ${x+ "$@" "$@" }; }
empty_pair() { x=1; show ${x+"" ""}; }
field_then_empty() { x=1; show ${x+a ""}; }
at_then_empty() { x=1; show ${x+"$@" ""}; }
empty_then_at() { x=1; show ${x+"" "$@"}; }
at_space_empty_quote() { x=1; show ${x+"$@"" "}; }

cases="slice_at slice_at_default slice_at_affixed slice_at_length"
cases="$cases slice_at_empty_length slice_at_unquoted slice_at_outer_quoted"
cases="$cases slice_at_outer_mixed slice_at_after_field slice_array"
cases="$cases slice_array_length slice_array_negative slice_array_unquoted"
cases="$cases slice_array_default slice_array_outer_quoted slice_array_affixed"
cases="$cases slice_both hash_plus hash_colon_plus hash_outer_quoted"
cases="$cases hash_affixed hash_mixed hash_minus hash_literal status_plus"
cases="$cases status_colon_minus unused_positional used_positional"
cases="$cases used_positional_quoted default_positional double_at"
cases="$cases double_at_quoted leading_space_quoted leading_space_at empty_pair"
cases="$cases field_then_empty at_then_empty empty_then_at at_space_empty_quote"

for case_name in $cases; do
  printf '== %s\n' "$case_name"
  "$case_name" "a b" "c d" "e"
  "$case_name"
  "$case_name" "x  y"
  "$case_name" "" "z"
done
