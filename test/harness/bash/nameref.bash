#!/bin/bash
# Name references, checked against bash. A reference writes through for a
# plain, appending, element, array, read, printf, arithmetic, and prefix
# assignment, follows a chain, passes a caller variable by reference, binds
# per word in a for loop, and reports itself to declare -p, ${!name},
# [[ -R ]], test -R, and [ -R ].
# unset acts on the target, unset -n on the reference, and a circular chain
# reads as unset and fails an assignment.
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
