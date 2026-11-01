#!/bin/bash
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cd "$dir" || exit 1

echo "--- three test arguments make -a and -o binary ---"
[ -n -a -n ]; echo "$?"
[ -f -o "" ]; echo "$?"
[ "" -a x ]; echo "$?"
[ ! -a ! ]; echo "$?"

echo "--- declare through a name reference reaches the target ---"
declare -n assoc_ref=assoc_target
declare -A assoc_ref
assoc_ref[a]=1
declare -p assoc_target
declare -n indexed_ref=indexed_target
declare -a indexed_ref
declare -p indexed_target

echo "--- declare through a name reference keeps an existing array ---"
existing=(a b)
declare -n existing_ref=existing
declare -A existing_ref 2>/dev/null; echo "$?"
declare -p existing
declare -a existing_ref=2
declare -p existing

scalar=q
declare -a scalar+=r
declare -p scalar

echo "--- unset empties a whole array through at and star ---"
whole=(1 2 3)
unset 'whole[@]'
declare -p whole
declare -A literal_keys=([a]=1)
unset 'literal_keys[*]'
echo "${#literal_keys[@]}"

echo "--- pattern replacement skips an unset variable ---"
unset missing
echo "[${missing//*/x}] [${missing/*/x}] [${missing/#*/x}]"
empty=
echo "[${empty//*/x}]"

echo "--- extended replacement stays fast on a long value ---"
shopt -s extglob
value=$(printf 'ab%.0s' {1..2000})
replaced=${value//@(a|b)/--}
echo "${#replaced}"
replaced=${value//?(a)/x}
echo "${#replaced}"
quoted='x(abc)'
echo "${quoted/?"("abc)/R}"

echo "--- a continued heredoc line hides the delimiter ---"
cat <<E
foo\
E
E
cat <<"E"
kept\
E
cat <<E
\
E
echo after-joined-delimiter

echo "--- a regex variable keeps its backslashes ---"
word='\<bar'; [[ 'foo bar' =~ $word ]]; echo "$?"
class='\w'; [[ a =~ $class ]]; echo "$?"
back='(a)\1'; [[ aa =~ $back ]]; echo "$?"
dot='a\.c'; [[ abc =~ $dot ]]; echo "$?"

echo "--- an explicit set -u covers test operands ---"
( set -u; [ -z "$unset_operand" ]; echo reached ) 2>/dev/null; echo "$?"
( set -u; [[ -z $unset_operand ]]; echo reached ) 2>/dev/null; echo "$?"
