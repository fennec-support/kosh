#!/bin/bash
# Name references, checked against bash. A reference writes through for a
# plain, appending, element, array, read, printf, arithmetic, and prefix
# assignment, follows a chain, passes a caller variable by reference, binds
# per word in a for loop, and reports itself to declare -p, ${!name},
# [[ -R ]], test -R, and [ -R ].
# unset acts on the target, unset -n on the reference and on nothing else, and
# a circular chain reads as unset and fails an assignment. Through a circular
# chain read, printf -v, and export fail, arithmetic, a prefix before a
# builtin, and declare skip the write, a prefix before eval, a function, or an
# external command binds a plain variable for that command only, an element or
# array assignment and unset drop the
# reference, and a function's reference that loops back writes the caller's
# variable. A reference to itself is an error at
# the top level, and inside a function it warns and reaches the variable
# outside the function. RANDOM and SECONDS take the attribute and keep their
# generated value, so each expansion or assignment of the name fails. An
# append through a reference adds when the target is an integer.
X=1
declare -n r=X
r=2
echo "assign X=$X r=$r ${r} ${#r}"
r+=3
echo "append X=$X"
read -r r <<<"readval"
echo "read X=$X"
printf -v r '%s' pv
echo "printf X=$X"
declare -p r X
echo "indirect ${!r}"

N=5
declare -n m=N
echo "arith $((m + 1))"
((m++))
echo "after increment N=$N"

P=old
declare -n pr=P
pr=temporary printenv P
echo "prefix P=$P"

a=(x y z)
declare -n ra=a
ra[1]=Q
echo "element ${a[*]} ${ra[1]} ${#ra[@]} ${!ra[@]}"
ra+=(w)
echo "array append ${a[*]}"
for item in "${ra[@]}"; do
  echo "field $item"
done
unset 'ra[0]'
declare -p a

declare -n e='a[2]'
echo "element reference $e"
e=ZZ
echo "after element reference ${a[*]}"
declare -p e

declare -A assoc
declare -n ar=assoc
ar[key]=value
echo "associative ${assoc[key]} ${!ar[@]}"

c3=deep
declare -n c2=c3
declare -n c1=c2
echo "chain $c1 ${!c1}"
c1=set
echo "chain write c3=$c3"

set_by_reference() {
  local -n out=$1
  out=from_function
}
Y=
set_by_reference Y
echo "function Y=$Y"

append_by_reference() {
  local -n list=$1
  list+=(new)
  echo "inside ${#list[@]}"
}
b=(1 2)
append_by_reference b
echo "b=${b[*]}"

change_local() {
  local x=local
  local -n p=x
  p=changed
  echo "local x=$x"
}
x=global
change_local
echo "global x=$x"

declare -n loop
for loop in X P; do
  echo "loop $loop"
done
declare -p loop

declare -n unbound
echo "unbound ${unbound-unset}"
unbound=Z
declare -p unbound

T=
declare -n through=T
declare through=declared
echo "declare through T=$T"
export through
printenv T

declare +n through
declare -p through

declare -n gone=X
unset gone
echo "unset target X=${X-unset}"
declare -p gone
X=back
unset -n gone
echo "unset reference X=$X"
declare -p gone 2>/dev/null || echo "reference removed"

[[ -R r ]] && echo "r is a reference"
[[ -R X ]] || echo "X is not a reference"
declare -n dangling
for name in r X e loop dangling unset_name; do
  test -R "$name"
  test_status=$?
  [ -R "$name" ]
  echo "test -R $name $test_status $?"
done
test ! -R r; echo "negated test -R $?"
[ -R r -a -R X ]; echo "joined test -R $?"
[ -R r -o -R X ]; echo "either test -R $?"
test -R; echo "lone -R $?"

declare -n self=self 2>/dev/null
echo "self status $?"
declare -n bad=1abc 2>/dev/null
echo "invalid status $?"
pp=1
declare -n pp 2>/dev/null
echo "invalid value status $?"
declare -p pp

declare -n s1='a;b' 2>/dev/null
echo "separator target status $?"
declare -n s2='a[1' 2>/dev/null
echo "unclosed target status $?"
declare -n s3='a]' 2>/dev/null
echo "bracket target status $?"

sub=(zero one two)
declare -n se='sub[$(echo evaluated >&2; echo 1)]'
{ echo "subscript read $se"; } 2>&1
{ se=written; } 2>&1
echo "subscript write ${sub[*]}"

readonly RO=kept
declare -n ro=RO
(ro=changed; echo "not reached") 2>/dev/null
echo "readonly assignment status $?"
(ro+=more; echo "not reached") 2>/dev/null
echo "readonly append status $?"
(ro[1]=element; echo "not reached") 2>/dev/null
echo "readonly element status $?"
read -r ro <<<"read" 2>/dev/null
echo "readonly read status $?"
printf -v ro '%s' printed 2>/dev/null
echo "readonly printf status $?"
unset ro 2>/dev/null
echo "readonly unset status $?"
write_readonly() {
  local -n target=RO
  target=from_function
  echo "not reached"
}
(write_readonly) 2>/dev/null
echo "readonly local status $?"
declare -n RO=other 2>/dev/null
echo "readonly reference status $?"
echo "RO=$RO"
declare -p RO

W=writable
declare -n protect=W
readonly protect
(W=changed; echo "not reached") 2>/dev/null
echo "readonly through reference status $? W=$W"
declare -p protect W

declare -i counter=1
declare -n counted=counter
counted='2 + 3'
declare -u upper
declare -n cased=upper
cased=value
echo "target attributes counter=$counter upper=$upper"

declare -n ca=cb
declare -n cb=ca
{ echo "circular ${ca-default}"; } 2>/dev/null
(ca=1; echo "not reached") 2>/dev/null
echo "circular assignment status $?"
{
  read -r ca <<<"r"; echo "circular read $?"
  printf -v ca '%s' p; echo "circular printf $?"
  export ca=e; echo "circular export $?"
  (( ca = 1 )); echo "circular arithmetic $?"
  ca=prefix true; echo "circular prefix $?"
  ca=pre eval 'echo "circular prefix eval [$ca]"'
  circular_prefix_function() { echo "circular prefix function [$ca]"; }
  ca=pre circular_prefix_function
  ca=pre env | grep '^ca='
  declare ca=d; echo "circular declare $?"
  (ca[1]=element; echo "circular element $?"; declare -p ca cb)
  (ca=(a b); echo "circular array $?"; declare -p ca cb)
  (unset ca; echo "circular unset $?"; declare -p cb; [[ -v ca ]] || echo "unset ca")
} 2>/dev/null
declare -p ca cb
circular_inner() { local -n circular_x=$1; circular_x=inner; echo "inner $?"; }
circular_outer() {
  local circular_x=outer
  local -n circular_x2=circular_x
  circular_inner circular_x2
  echo "outer circular_x=$circular_x"
}
circular_outer 2>/dev/null
plain_unset_n=1
unset -n plain_unset_n; echo "unset -n plain $? [$plain_unset_n]"
plain_unset_n_function() { echo function; }
unset -n plain_unset_n_function; plain_unset_n_function

self_scalar() {
  local -n selfref=selfref
  echo "local self status $? [$selfref] $(declare -p selfref)"
  [[ -R selfref ]] && test -R selfref && echo "self is a reference"
  selfref=written
  echo "after write [$selfref]"
}
selfref=global
self_scalar 2>/dev/null
echo "global selfref=$selfref"
declare -p selfref
append_self() {
  local -n selflist=$1
  selflist+=(new)
  echo "inside ${#selflist[@]} ${selflist[*]}"
  read -ra selflist <<<"r1 r2"
}
selflist=(a b)
append_self selflist 2>/dev/null
declare -p selflist
self_outer() { declare -n selfdeep=selfdeep; self_inner; echo "outer $selfdeep"; }
self_inner() { echo "inner [$selfdeep]"; selfdeep=from_inner; declare -p selfdeep; }
selfdeep=top
self_outer 2>/dev/null
echo "top selfdeep=$selfdeep"
self_arith() { local -n selfnum=selfnum; ((selfnum += 5)); printf -v selfnum '%s!' "$selfnum"; }
selfnum=1
self_arith 2>/dev/null
echo "selfnum=$selfnum"
self_over_local() {
  local selfover=local
  local -n selfover=selfover
  echo "over local status $? [${selfover-unset}]"
  selfover=assigned
}
self_over_local 2>/dev/null
declare -p selfover
self_bind() { local -n selfbind; selfbind=selfbind; echo "bind status $? [$selfbind]"; selfbind=val; }
selfbind=gl
self_bind 2>/dev/null
echo "selfbind=$selfbind"
self_unset() { local -n selfgone=selfgone; unset selfgone; echo "unset $? [${selfgone-unset}]"; selfgone=local; }
selfgone=kept
self_unset 2>/dev/null
echo "selfgone=$selfgone"
self_unset_n() { local -n selfn=selfn; unset -n selfn; echo "unset -n $? [${selfn-unset}]"; selfn=local; }
selfn=kept
self_unset_n 2>/dev/null
echo "selfn=$selfn"
self_listing() { local before=1; local -n selflisted=selflisted; local -p; }
self_listing 2>/dev/null
self_retarget() { local -n selfrt=selfrt; declare -n selfrt=X; echo "retarget [$selfrt]"; }
self_retarget 2>/dev/null
self_subshell() { local -n selfsub=selfsub; (selfsub=inner; echo "subshell [$selfsub]"); echo "after [$selfsub]"; }
selfsub=s
self_subshell 2>/dev/null
self_indirect() { local -n selfbang=selfbang; echo "${!selfbang}"; echo "not reached"; }
selfbang=X
self_indirect 2>/dev/null; echo "indirect status $?"
echo "indirect line status $?"
self_global() { declare -gn selfg=selfg; echo "global self status $?"; }
self_global 2>/dev/null
declare -p selfg
{ echo "global circular [${selfg-unset}]"; } 2>/dev/null
declare -n selftop; selftop=selftop; echo "top bind status $?"
declare -p selftop
self_readonly() { local -n RO=RO; echo "readonly self status $?"; }
self_readonly 2>/dev/null

dynamic_target=fixed
declare -n RANDOM=dynamic_target; echo "random reference status $?"
echo "read $RANDOM"; echo "not reached"
echo "random read line status $?"
RANDOM=5; echo "not reached"
echo "random assignment line status $?"
RANDOM+=1; echo "not reached"
echo "random append line status $?"
x=${RANDOM-default}; echo "not reached"
echo "random default line status $?"
(echo "$RANDOM"; echo "not reached"); echo "random subshell status $?"
random_reader() { echo "in function"; echo "$RANDOM"; echo "not reached"; }
random_reader; echo "not reached"
echo "random function line status $?"
echo "arithmetic $((RANDOM >= 0))"
read -r RANDOM <<<"x"; echo "random read status $?"
printf -v RANDOM '%s' y; echo "random printf status $?"
declare RANDOM=4; echo "random declare status $?"
for RANDOM in word; do echo "random loop"; done; echo "random for status $?"
[[ -v RANDOM ]]; echo "random -v $?"
[[ -R RANDOM ]] && test -R RANDOM && [ -R RANDOM ] && echo "random is a reference"
bang=${!RANDOM}
[[ $bang =~ ^[0-9]+$ ]] && echo "random indirect is a number"
unset RANDOM; echo "random unset status $?"
echo "$RANDOM"; echo "not reached"
echo "random after unset line status $?"
echo "dynamic_target=$dynamic_target"
unset -n RANDOM; echo "random unset -n status $?"
echo "random after unset -n [${RANDOM-gone}]"
declare -n SECONDS; echo "seconds bare status $?"
declare -n SECONDS=dynamic_target
declare +n SECONDS; echo "seconds +n status $?"
[[ $SECONDS =~ ^[0-9]+$ ]] && echo "seconds counts again"
seconds_local() {
  local -n SECONDS=dynamic_target
  echo "local seconds [$SECONDS]"
  SECONDS=local_write
}
seconds_local
echo "dynamic_target=$dynamic_target"
[[ $SECONDS =~ ^[0-9]+$ ]] && echo "seconds still counts"
seconds_local_unset() { local -n SECONDS=dynamic_target; unset -n SECONDS; }
seconds_local_unset
[[ $SECONDS =~ ^[0-9]+$ ]] && echo "seconds counts after a local unset -n"
declare -i int_target=10
declare -n int_ref=int_target
int_ref+=5
echo "integer append $int_target"
int_ref+=1 eval 'echo "integer prefix append $int_target"'
declare int_ref+=2
echo "integer declare append $int_target"
export int_ref+=3
echo "integer export append $int_target"
declare -ai int_array=(1)
declare -n int_array_ref=int_array
int_array_ref+=(3+4)
declare -p int_array
int_append_local() {
  local -n int_local=$1
  int_local+=10
}
declare -i int_caller=1
int_append_local int_caller
echo "integer local reference append $int_caller"
