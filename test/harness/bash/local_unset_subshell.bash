#!/bin/bash
# A subshell that unsets a local of a calling function reveals the value the
# local shadowed, while a local of the running function stays unset. Scalars,
# indexed and associative arrays, and exported variables each come back intact.
# Variable-table collisions, reused function frames, and local lookups also
# match Bash.

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

collision_name_14=14
collision_name_24=24
collision_name_34=34
collision_name_44=44
collision_name_54=54
collision_name_64=64
collision_name_74=74
collision_name_84=84
collision_name_94=94
unset collision_name_24 collision_name_44 collision_name_64 collision_name_84
collision_name_11009=11009
collision_name_11019=11019
(
    collision_name_14=child
    unset collision_name_34
)
printf 'collisions=%s,%s,%s,%s,%s,%s,%s\n' \
    "$collision_name_14" "$collision_name_34" "$collision_name_54" \
    "$collision_name_74" "$collision_name_94" \
    "$collision_name_11009" "$collision_name_11019"

reuse_deep_frame() {
    local frame_value=$1
    if [ "$1" -gt 0 ]; then
        reuse_deep_frame "$((frame_value - 1))"
    else
        printf 'deep-frame=%s\n' "$frame_value"
    fi
}
reuse_shallow_frame() {
    local frame_value=$1
    printf 'shallow-frame=%s\n' "$frame_value"
}
reuse_deep_frame 12
reuse_shallow_frame first
reuse_shallow_frame second

export inherited_local=environment
indexed_local=(zero one)
local_lookup_probe() {
    local inherited_local ordinary_unset_local IFS indexed_local
    printf 'local-lookups=%s,%s,%s,%s\n' "$inherited_local" \
        "${ordinary_unset_local-unset}" "${#IFS}" "$indexed_local"
}
local_lookup_probe
