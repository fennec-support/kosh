#!/bin/bash
# A subshell that unsets a local of a calling function reveals the value the
# local shadowed, while a local of the running function stays unset. Scalars,
# indexed and associative arrays, and exported names each come back intact.

x=global
inner() {
  ( unset x; echo "background=${x-unset}" ) &
  wait
  echo x | ( unset x; echo "pipe=${x-unset}" )
}
outer() {
  local x=local
  inner
  echo "outer=$x"
}
outer

own() {
  local y=own
  ( unset y; echo "own=${y-unset}" ) &
  wait
}
y=global
own

twice() {
  local z=middle
  shadow
}
shadow() {
  local z=inner
  ( unset z; echo "first=${z-unset}"; unset z; echo "second=${z-unset}" ) &
  wait
}
z=global
twice

reveal_arrays() {
  ( unset list map; echo "list=${list[*]-unset} map=${map[k]-unset}" ) &
  wait
}
arrays() {
  local -a list=(l1 l2)
  declare -A map=([k]=local)
  reveal_arrays
}
list=(g1 g2)
declare -A map=([k]=global)
arrays

reveal_exported() {
  ( unset e; echo "e=${e-unset}"; declare -p e; e=2+3; echo "assigned=$e" ) &
  wait
}
exported() {
  local -i e=5
  reveal_exported
}
export e=exported
exported
