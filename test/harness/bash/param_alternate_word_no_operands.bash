#!/usr/bin/env bash
# The alternate-word cases of sh/param_alternate_word_fields.sh and
# sh/param_alternate_word_special.sh, called without operands and checked
# against bash. A quoted "$@" with no positional parameters yields no field.
show() {
  printf '%s:' "$#"
  for a in "$@"; do printf '[%s]' "$a"; done
  printf '\n'
}

at_plus() { show ${1+"$@"}; }
at_colon_plus() { show ${1:+"$@"}; }
at_minus() { show ${missing-"$@"}; }
at_colon_minus() { show ${missing:-"$@"}; }
star_plus() { x=1; show ${x+"$*"}; }
literal_plus() { x=1; show ${x+"a b"}; }
single_plus() { x=1; show ${x+'a b'}; }
mixed_plus() { x=1; show ${x+"$@" tail}; }
unquoted_plus() { x=1; show ${x+$@}; }
quoted_plus() { x=1; show ${x+"$@"}; }
affixed_plus() { x=1; show ${x+pre"$@"post}; }
outer_quoted() { x=1; show "${x+"$@"}"; }
unset_plus() { show ${missing+"$@"}; }
empty_colon_plus() { x=; show ${x:+"$@"}; }
set_minus() { x=1; show ${x-"$@"}; }
tail_minus() { show ${missing-"a b" c}; }
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

cases="at_plus at_colon_plus at_minus at_colon_minus star_plus literal_plus"
cases="$cases single_plus mixed_plus unquoted_plus quoted_plus affixed_plus"
cases="$cases outer_quoted unset_plus empty_colon_plus set_minus tail_minus"
cases="$cases hash_plus hash_colon_plus hash_outer_quoted hash_affixed"
cases="$cases hash_mixed hash_minus status_plus status_colon_minus"
cases="$cases unused_positional used_positional used_positional_quoted"
cases="$cases default_positional double_at double_at_quoted"
cases="$cases leading_space_quoted leading_space_at empty_pair"
cases="$cases field_then_empty at_then_empty empty_then_at"

for case_name in $cases; do
  printf '== %s\n' "$case_name"
  "$case_name"
done
