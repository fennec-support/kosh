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

echo "--- a prefix assignment stays for export and readonly operands ---"
prefixed=1 export prefixed; echo "[$prefixed]"
fixed=2 readonly fixed; echo "[$fixed]"
other=3 export unrelated; echo "[${other-unset}]"

echo "--- an unset IFS expands as unset and still splits ---"
( unset IFS; echo "[${IFS-u}]"; words='a b'; printf '[%s]' $words; echo
  IFS=: true; echo "[${IFS-u}]" )

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

echo "--- export keeps the special variables working ---"
( IFS=:; export IFS; echo "[$IFS]"; v=a:b; printf '<%s>' $v; echo )
( export IFS=:; v=a:b; printf '<%s>' $v; echo "[$IFS]" )
( touch pa qb; export GLOBIGNORE='p*'; echo *; rm pa qb )

echo "--- a nested operand keeps its quoting ---"
v=1
printf '[%s]' ${v+${v+"a b"}} ${v+${u-'a b'}} ${u-${u-a\ b}}; echo
printf '[%s]' ${u-${u-$'a b'}} ${v+${v+:'a b'}a}; echo
printf '[%s]' ${v+${v+${v+"a b"}}}; echo
touch ab
printf '[%s]' ${v+${v+"a*"}} ${v+${v+a*}}; echo
rm ab

echo "--- a nested operand keeps one field per parameter ---"
set -- 'a b' c
printf '[%s]' ${U-"${1+"$@"}"}; echo
printf '[%s]' "${U:-${1+"$@"}}"; echo
printf '[%s]' ${U:-"${@-d}"}; echo
printf '[%s]' ${U:-""${1+"$@"}""}; echo
printf '[%s]' ${u-${v+"$@"}}; echo
printf '[%s]' ${u-${1+"$*"}}; echo
list=('a b' c)
printf '[%s]' ${u-${list+"${list[@]}"}}; echo
set --

echo "--- a tilde result is literal ---"
( touch ab; HOME='a*'; echo ~; case ab in ~) echo match;; *) echo none;; esac
  set -- ${u:-~}; echo "$1"; rm ab )
( HOME='a  b'; set -- ${u:-~}; echo $#; set -- ${u:-~/x}; echo $# )
( HOME=/h; declare -A M=([k]=~); echo "${M[k]}"; a=(x=~ [3]=~ ~); declare -p a )

echo "--- repeated globstar components collapse ---"
( shopt -s globstar; mkdir -p g/b; touch g/b/f g/f; cd g
  echo **/**/f; echo **/**; echo **/**/; echo **/**/**/**/**/**/**/**/**/**/**/**/f
  cd ..; rm -r g )

echo "--- a prefix assignment leaves an array intact ---"
A=(1 2 3)
show_a() { echo "$A"; }
A=9 show_a; declare -p A
A=9 eval 'echo $A'; declare -p A
store_a() { A=7; }
A=9 store_a; declare -p A
drop_a() { unset A; }
A=9 drop_a; declare -p A
declare -A H=([k]=v)
A=(1); H=9 eval 'declare -p H'; declare -p H

echo "--- declare builds an associative array through a subscript or reference ---"
declare -A 'r1[key]=x'; declare -p r1
declare -n rn1=zq; declare -A rn1=([k]=v); declare -p zq rn1

echo "--- read and local accept array elements ---"
read 'ra[1]' 'ra[2]' <<<'x y'; declare -p ra
declare -A rh; read 'rh[k]' <<<x; declare -p rh
local_elements() { local q[3]=x; local 'z[4]=l'; local w[2]; declare -p q z w; }
local_elements

echo "--- only an unquoted subscript in source is a key ---"
a=(x '[3]=y'); declare -p a
a=(\[1]=x); declare -p a
a=([1]\=x); declare -p a
v='[k]=v'; declare -A k1=($v); declare -p k1
declare -A k2=("[a]=1" '[b]=2'); declare -p k2
d=(); d+=([b]=2 [a]+=x); declare -p d
a=([a[0]=9]=x); declare -p a
a=([0]=x [a[0]+1]=y); declare -p a
declare -A k3=([a]=1); k3+=([a]+=x); declare -p k3
a=(1 2); declare -n a=b 2>/dev/null; echo "$?"; declare -p a

echo "--- a bracket with an escaped closer stays literal ---"
v='[a]'; echo "${v/[a\]/<>}" "${v//[a\]/<>}"
v='x[a]y'; echo "${v/[a\]/<>}"

echo "--- a file substitution sets the status ---"
false; x=$(</dev/null); echo "$?"
x=$(<missing_file) 2>/dev/null; echo "[$x] $?"

echo "--- read trims a trailing delimiter and joins escaped newlines ---"
( IFS=' :'; read a <<< 'x :'; printf '[%s]' "$a"; read a b <<< 'x : y :'; printf '[%s]' "$a" "$b"; echo )
printf 'x\\\ny\0' | { read -d '' x; printf '[%s]\n' "$x"; }
printf 'x\\\ny:' | { read -d : x; printf '[%s]\n' "$x"; }
( IFS=; set -- '' ''; printf '[%s]' "${*:+d}" "${*:-d}"; echo )

echo "--- local attributes and special variables ---"
secs() { local SECONDS=10; unset SECONDS; }
secs; echo "[${SECONDS:+set}]"
minus_i() { local +i n=1+1; echo "$n"; }
declare -i n=1; minus_i; echo "$n"
minus_x() { local +x PATH; declare -p PATH | head -c 10; echo; }
minus_x
aliases_local() { local BASH_ALIASES; BASH_ALIASES[x]=y; }
aliases_local; alias

echo "--- transforms through a reference and the positional list ---"
declare -a ra2=(1 2); declare -n rr=ra2; echo "${rr[1]@a}"
declare -n rt=a[1]; a=(x y); echo "[${rt@a}]"
x=1; set -- x; echo "${!@@Q}" "${!*@Q}"; set --
tz=q; declare -n ref1=tz; echo "${!ref1-d}" "${!ref1@Q}"
declare -n ref2=unset_target; echo "${!ref2-d}" "${!ref2@Q}"

echo "--- ansi-c quoting ends at the first nul ---"
x=$'a\0b'; echo "${#x}"; y="$x"; echo "${#y}"; echo $'b\x00c' "d"

echo "--- a function substitution shares loops and scopes locals ---"
for i in 1 2; do x=${ break; }; echo "in $i"; done; echo out
for i in 1 2; do x=${ continue; }; echo "in $i"; done; echo out
f() { local v=1; x=${ local v=2; echo "$v"; }; echo "$x $v"; }; f
g() { for i in 1 2; do x=${ return 3; echo no; }; echo "in $i $?"; done; }; g
v=1; x=${ local v=2; echo "$v"; }; echo "$x $v"

echo "--- exec in a subshell replaces it ---"
( trap 'echo sub' EXIT; exec true ); echo "$?"

echo "--- a nested mapfile keeps the outer callback ---"
mapfile -t -C 'mapfile -t -C : -c 1 y <<< 1; echo cb' -c 1 z < <(echo 1; echo 2); echo "${z[*]}"

echo "--- a failing alias reports one ERR ---"
shopt -s expand_aliases
alias failing=false
trap 'echo err' ERR
eval failing
trap - ERR
unalias failing

echo "--- an alias inside its own process substitution ---"
alias reader=cat
eval 'reader <(reader /dev/null); echo "$?"'
unalias reader

echo "--- a bang after a test operator is an operand ---"
x='!'
[[ $x == ! ]]; echo "$?"
[[ a != ! ]]; echo "$?"
[[ -n ! ]]; echo "$?"
[[ ( a == ! ) ]]; echo "$?"
[[ a == ! && 1 ]]; echo "$?"
[[ a < ! ]]; echo "$?"

echo "--- a bang before a term negates it ---"
a=1 b=c
! true | true; echo "$?"
[[ ! -z x ]]; echo "$?"
[[ ! a == ! ]]; echo "$?"
[[ -n $a && ( $b == c ) ]]; echo "$?"

echo "--- a hash in a regular expression operand ---"
[[ x =~ (x)#y ]]; echo "$?"
[[ 'x#y' =~ (x)#y ]]; echo "$?"
[[ 'a #b' =~ (a #b) ]]; echo "$?"
[[ abc =~ ^(a|b)+(c)$ ]]; echo "$? ${BASH_REMATCH[2]}"

echo "--- a keyword after a redirection operator is a word ---"
(
  mkdir keywords
  cd keywords || exit 1
  for i in 1 2; do echo "$i" >done; done
  cat done
  if true; then echo a >|then; fi
  cat then
  echo b >&fi
  cat fi
  echo c <>esac
  while true; do echo d 2>do; break; done
  ls
  cat <<done
in a loop keyword
done
  if true; then
    cat <<fi
in a branch keyword
fi
  fi
  cat <<-esac
	stripped
	esac
)

echo "--- a heredoc inside a parameter expansion ---"
unset x
echo "${x:-$(cat <<EOF
}
EOF
)}"

echo "--- the keys of an array ---"
a=(x y z)
echo "${!a[@]}"
declare -A m=([k]=1)
echo "${!m[@]}"

echo "--- a call prefix leaves no exported array element ---"
export exported_list=9
exported_list=(1 2)
list_reader() { :; }
exported_list=x list_reader
/usr/bin/env | grep -c '^exported_list='
echo "${exported_list[1]}"
unset exported_list

echo "--- an unset IFS stays unset after a command substitution ---"
unset IFS
captured=$(echo hi)
echo "[${IFS-unset}]"
IFS=$' \t\n'

echo "--- an export operand keeps its own value over a prefix ---"
PREFIXED=1 export PREFIXED=2
echo "$PREFIXED"
/usr/bin/env | grep '^PREFIXED='
unset PREFIXED
PREFIXED=1 export PREFIXED+=2
echo "$PREFIXED"
unset PREFIXED
PREFIXED=1 export -- PREFIXED=2
echo "$PREFIXED"
unset PREFIXED
PREFIXED=1 export -n PREFIXED
echo "[$PREFIXED]"
PREFIXED=1 export -f list_reader
echo "[$PREFIXED]"
unset PREFIXED
PATH=/bin export PATH=/usr/bin:/bin
echo "$PATH"

echo "--- a local keeps its value while it is exported ---"
local_export() {
  local value=7
  export value
  echo "[$value]"
  env | grep '^value='
  value=8
  env | grep '^value='
}
local_export
echo "[${value-unset}]"
env | grep -c '^value='
local_export_later() {
  local late
  late=9
  export late
  echo "[$late]"
  local again=3
  export again=4
  echo "[$again]"
  env | grep '^again='
}
local_export_later
outer=kept
local_export_shadow() {
  local outer=inner
  export outer
  env | grep '^outer='
}
local_export_shadow
echo "[$outer]"
env | grep -c '^outer='
export carried=outside
local_export_carried() {
  local carried=inner
  export carried
  env | grep '^carried='
}
local_export_carried
echo "[$carried]"
env | grep '^carried='
unset carried
local_export_in_callee() {
  export callee_value
}
local_export_caller() {
  local callee_value=5
  local_export_in_callee
  echo "[$callee_value]"
  env | grep '^callee_value='
}
local_export_caller

echo "--- a wrapped declaration keeps its value and its export ---"
unset A
A=1 command export A=2
echo "$A"
env | grep '^A='
unset A
A=1 builtin export A=2
echo "$A"
env | grep '^A='
unset A
A=1 command -p export B=3 A
echo "$A"
env | grep '^A='
unset A B
A=1 declare -x A=2
declare -p A
env | grep '^A='
unset A

echo "--- a prefix on a command keeps an exported array exported ---"
declare -a exported_array=(1 2)
export exported_array
exported_array=x true
declare -p exported_array
export exported_unset
exported_unset=1 true
declare -p exported_unset

echo "--- a lone operator is an operand and a second bang is a syntax error ---"
[[ a == ! ]]; echo "$?"
[[ ! -n ! ]]; echo "$?"
"$BASH" -c '[[ ! == ! ]]; echo after' 2>/dev/null; echo "$?"
"$BASH" -c '[[ ! = ! ]]; echo after' 2>/dev/null; echo "$?"

echo "--- an EXIT trap set in a pipeline stage runs in that stage ---"
{ trap 'echo stage' EXIT; } | cat
{ trap 'exit 4' EXIT; true; } | cat
echo "${PIPESTATUS[0]}"
