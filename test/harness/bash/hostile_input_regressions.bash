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
declare -i number=1
declare -a number+=2
declare -p number

echo "--- indirect positional parameters ---"
sparse=(x y z)
declare -A spaced=(["a b"]=1)
set -- "sparse[1 + 1]"
echo "<${!@}>"
set -- "spaced[a b]"
echo "<${!*}>"
set --
echo "<${!@}>"
( set -- a-b; echo "${!@}"; echo unreachable ) 2>/dev/null; echo "$?"

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
cat <<E
E\

echo after-empty-join
cat <<EOF
abc
E\
O\
F
echo after-long-join
cat <<OF
E\
O\
F
kept
OF
cat <<E
a\
E\

body
E
cat <<-E
	E\

	echo kept
	echo E
echo after-tab-join

echo "--- getopts restarts on an index too large to hold ---"
OPTIND=99999999999999999999
getopts a opt -a
echo "$OPTIND $opt"
OPTIND=1

echo "--- a plain subscript reaches an indirect array ---"
indirect=(1 2)
plain='indirect[1]'
echo "${!plain}"

echo "--- a regex variable keeps its backslashes ---"
word='\<bar'; [[ 'foo bar' =~ $word ]]; echo "$?"
class='\w'; [[ a =~ $class ]]; echo "$?"
back='(a)\1'; [[ aa =~ $back ]]; echo "$?"
dot='a\.c'; [[ abc =~ $dot ]]; echo "$?"

echo "--- an explicit set -u covers test operands ---"
( set -u; [ -z "$unset_operand" ]; echo reached ) 2>/dev/null; echo "$?"
( set -u; [[ -z $unset_operand ]]; echo reached ) 2>/dev/null; echo "$?"

echo "--- printf treats a negative precision as omitted ---"
printf '%.*d|\n' -1 42
printf '%.*s|\n' -1 abc

echo "--- printf -v takes an attached word ---"
printf -vattached %s v; echo "$attached"

echo "--- printf format escapes drop the backslash ---"
printf '\"|\?|\x27|\n'
printf "\\'|\n"

echo "--- printf writes hexadecimal floats ---"
printf '%a %A\n' 3.25 0.5

echo "--- printf %T falls back for a time without a date ---"
TZ=UTC0 printf '%(%s)T\n' 9223372036854775807

echo "--- printf -v rejects a whole array subscript ---"
printf -v 'whole[*]' x 2>/dev/null; echo "$?"
printf -v 'whole[@]' x 2>/dev/null; echo "$?"

echo "--- printf writes a long escape as extended UTF-8 ---"
printf '\U7fffffff' | od -An -tx1

echo "--- bracket members read ranges, equivalence, and collation ---"
shopt -s extglob
for subject in - a 5 b d '|' abcd; do
  for pattern in '[--a]' '[[=a=]]' '[[.a.]]' '[[.a.]-c]' '@([a|b]|abcd)'; do
    case $subject in $pattern) echo "$subject $pattern" ;; esac
  done
done

echo "--- quoted words inside a double-quoted parameter word ---"
echo "${x:-"$(echo '"')"}"
echo "${x:-"$(echo "}")"}"
echo "${x:-"`echo "}"`"}"
echo "${y:-'"'}"
echo "${x:-"${y:-"}"}"}"
echo ${x:-$'\'}'}

echo "--- a parameter expansion inside a substitution ---"
echo "$(echo ${x:-)})"
echo "$(echo ${x:-"("})"
echo "$(echo "${x:-"}"}")"
echo "${x:-$(echo "${y:-"$(echo z)"}")}"

echo "--- a heredoc delimiter continued over a line ---"
x=$(cat <<E\
F
hi
EF
)
echo "$x"
cat <<E\
F
${x}-body
EF

echo "--- strip tabs while the logical line is empty ---"
eval 'cat <<-E
	\
	E
echo reached'
echo "status $?"
cat <<-E
	a\
	b
	E

echo "--- arithmetic line continuation and comma branches ---"
echo $(( 1 + \
2 ))
(( 1 + \
2 )); echo "$?"
echo $((1\
2))
echo $(( 1 ? 2,3 : 4 ))
echo $(( 0 ? 2,3 : 4 ))

echo "--- digits are required after a base prefix ---"
( echo $((0b)) ) 2>/dev/null; echo "$?"
( echo $((16#)) ) 2>/dev/null; echo "$?"

echo "--- a command group after a double parenthesis ---"
unset x
echo $((echo a) | cat)
x=$((echo a) ); echo "$x"
echo "$((echo a) | cat)"
echo ${x:+$((echo b) | cat)}
echo `echo $((echo c) | cat)`

echo "--- conditional syntax is checked at parse time ---"
for source in '[[ ; ]]' '[[ & ]]' '[[ ;& ]]' '[[ ]]' '[[ && ]]' '[[ a | b ]]' \
  '[[ a && ]]' '[[ ! ]]' '[[ ( ]]' 'true | ! true' 'while ! do break; done' \
  'while time do break; done'
do
  eval "$source; echo unreachable" 2>/dev/null
  echo "status $?"
done

echo "--- a function prints a background compound command ---"
back() { while false; do :; done & wait; { :; } & wait; : & wait; }
declare -f back
