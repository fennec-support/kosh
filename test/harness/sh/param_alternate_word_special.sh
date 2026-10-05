#!/bin/sh
# The word of an alternate operator on the # and ? parameters keeps its quoted
# fields, and a positional parameter named inside the word stays silent when
# it is unset, checked byte-for-byte against dash.
show() {
  printf '%s:' "$#"
  for a in "$@"; do printf '[%s]' "$a"; done
  printf '\n'
}

hash_plus() { show ${#+"$@"}; }
hash_colon_plus() { show ${#:+"$@"}; }
hash_outer_quoted() { show "${#+"$@"}"; }
hash_affixed() { show ${#:+pre"$@"post}; }
hash_mixed() { show "${#:+"$@" tail}"; }
hash_minus() { show ${#-"$@"}; }
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

cases="hash_plus hash_colon_plus hash_outer_quoted hash_affixed hash_mixed"
cases="$cases hash_minus status_plus status_colon_minus unused_positional"
cases="$cases used_positional used_positional_quoted default_positional"
cases="$cases double_at double_at_quoted leading_space_quoted leading_space_at"
cases="$cases empty_pair field_then_empty at_then_empty empty_then_at"

for case_name in $cases; do
  printf '== %s\n' "$case_name"
  "$case_name" "a b" "c d" "e"
  "$case_name"
  "$case_name" "x  y"
  "$case_name" "" "z"
done
