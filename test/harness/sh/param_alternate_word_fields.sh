#!/bin/sh
# The word of a ${name+word} or ${name-word} operator keeps its own quoting when
# the expansion splits into fields, checked byte-for-byte against dash. A quoted
# "$@" inside the word yields one field per positional parameter, a quoted
# literal or "$*" stays one field, and an unquoted $@ splits. Calls without
# operands live in bash/param_alternate_word_no_operands.bash, since dash 0.5.13
# expands a quoted "$@" with no positional parameters to one empty field.
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

cases="at_plus at_colon_plus at_minus at_colon_minus star_plus literal_plus"
cases="$cases single_plus mixed_plus unquoted_plus quoted_plus affixed_plus"
cases="$cases outer_quoted unset_plus empty_colon_plus set_minus tail_minus"

for case_name in $cases; do
  printf '== %s\n' "$case_name"
  "$case_name" "a b" "c d"
  "$case_name" "x  y"
  "$case_name" "" "z"
done

f() { for a in ${1+"$@"}; do printf '<%s>' "$a"; done; echo; }
f "one two" three
f
set -- "k l" m
set -- ${1+"$@"} "n o"
echo "count=$#"
for a in "$@"; do printf '(%s)' "$a"; done; echo
