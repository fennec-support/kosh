#!/bin/bash
# The word of a ${name+word} or ${name-word} operator keeps its own quoting when
# the expansion splits into fields, checked byte-for-byte against bash. A quoted
# "$@" or "${array[@]}" inside the word yields one field per element, a quoted
# literal or "$*" stays one field, an unquoted $@ splits, and the empty and
# missing argument lists behave the same way for the positional and array forms.
# An element, an associative key, an indirect name, and a name reference as
# the subject give the word the same quoting.
show() {
  printf '%s:' "$#"
  for a in "$@"; do printf '[%s]' "$a"; done
  printf '\n'
}

arr=("p q" r)

at_plus() { show ${1+"$@"}; }
at_colon_plus() { show ${1:+"$@"}; }
at_minus() { show ${missing-"$@"}; }
at_colon_minus() { show ${missing:-"$@"}; }
star_plus() { x=1; show ${x+"$*"}; }
literal_plus() { x=1; show ${x+"a b"}; }
single_plus() { x=1; show ${x+'a b'}; }
escaped_plus() { x=1; show ${x+a\ b}; }
mixed_plus() { x=1; show ${x+"$@" tail}; }
unquoted_plus() { x=1; show ${x+$@}; }
quoted_plus() { x=1; show ${x+"$@"}; }
affixed_plus() { x=1; show ${x+pre"$@"post}; }
trailing_plus() { x=1; show ${x+"$@"} end; }
outer_quoted() { x=1; show "${x+"$@"}"; }
outer_quoted_bare() { x=1; show "${x+$@}"; }
outer_quoted_star() { x=1; show "${x+"$*"}"; }
double_at() { x=1; show ${x+"$@"}${x+"$@"}; }
braced_at() { x=1; show ${x+"${@}"}; }
array_plus() { x=1; show ${x+"${arr[@]}"}; }
array_star_plus() { x=1; show ${x+"${arr[*]}"}; }
unset_plus() { show ${missing+"$@"}; }
empty_colon_plus() { x=; show ${x:+"$@"}; }
set_minus() { x=1; show ${x-"$@"}; }
tail_minus() { show ${missing-"a b" c}; }
ifs_star() { x=1; IFS=:; show ${x+"$*"}; unset IFS; }
empty_quoted() { x=1; show ${x+""}; }
affixed_at_empty_quote() { x=1; show ${x+"$@"""}; }
element_plus() { show ${arr[1]:+"a b"} ${arr[0]:+"$1"}; }
element_minus() { i=0; show ${arr[i+5]-"a b"} ${arr[i++]-"c d"} "$i"; }
assoc_minus() { declare -A m=([k]=v); show ${m[k]-"a b"} ${m[none]-"c d"}; }
indirect_plus() { x=1 n=x; show ${!n:+"a b"} ${!n-"c d"}; }
indirect_element() { t='arr[1]'; show ${!t:+"a b"}; }
nameref_indirect() { declare -n r=arr; show ${!r:+"a b"}; }

cases="at_plus at_colon_plus at_minus at_colon_minus star_plus literal_plus"
cases="$cases single_plus escaped_plus mixed_plus unquoted_plus quoted_plus"
cases="$cases affixed_plus trailing_plus outer_quoted outer_quoted_bare"
cases="$cases outer_quoted_star double_at braced_at array_plus array_star_plus"
cases="$cases unset_plus empty_colon_plus set_minus tail_minus ifs_star"
cases="$cases empty_quoted affixed_at_empty_quote element_plus element_minus"
cases="$cases assoc_minus indirect_plus indirect_element nameref_indirect"

for case_name in $cases; do
  printf '== %s\n' "$case_name"
  "$case_name" "a b" "c d"
  "$case_name"
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
