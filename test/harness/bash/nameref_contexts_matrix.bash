#!/bin/bash
# Name references read in each word context and written by every binding
# command, checked against bash. A reference names a scalar, an unset name,
# an array element, an indexed array, an associative array, another
# reference, or a function local. Cases listed in the pending fixture are
# skipped.
export LC_ALL=C
list_only=1
. "${BASH_SOURCE%/*}/nameref_contexts_matrix_pending.bash"
unset list_only
cd "$(mktemp -d)" || exit 1
error_file=$PWD/.error

load_values() {
  unset -n ref inner
  unset ref inner target
  case $1 in
    scalar)
      target='a b*'
      declare -gn ref=target
      ;;
    unset_target)
      declare -gn ref=target
      ;;
    empty_target)
      target=
      declare -gn ref=target
      ;;
    element)
      target=(zero 'o ne' two)
      declare -gn ref='target[1]'
      ;;
    array)
      target=(zero 'o ne' '' three)
      declare -gn ref=target
      ;;
    sparse)
      target=([2]=two [9]='ni ne')
      declare -gn ref=target
      ;;
    assoc)
      unset target
      declare -gA target=([key]='v al')
      declare -gn ref=target
      ;;
    chain)
      printf -v target %s end
      declare -gn inner=target
      declare -gn ref=inner
      ;;
    integer)
      unset target
      declare -gi target=6
      declare -gn ref=target
      ;;
  esac
}

declare -A pending_set=()
for pending_key in "${pending_cases[@]}"; do
  pending_set[$pending_key]=1
done

run_case() {
  local case_key="$values|$operator||$context|$extra"
  if [[ -n ${pending_set[$case_key]-} ]]; then
    [ -n "${is_pending_run-}" ] || return 0
  else
    [ -z "${is_pending_run-}" ] || return 0
  fi
  printf '%s: ' "$1"
  eval "$1" 2>"$error_file"
  local status=$?
  if [ -s "$error_file" ]; then
    printf ' status=%s error\n' "$status"
  else
    printf ' status=%s\n' "$status"
  fi
}

item_pattern='^\[([^]]*)\]=("[^"]*"|\$'"'"'[^'"'"']*'"'"') (.*)$'

show_target() {
  local declaration head rest line sorted joined
  local -a items=()
  declaration=$(declare -p target 2>/dev/null) || declaration=unset
  if [[ $declaration == 'declare -A'*'=('* ]]; then
    head=${declaration%%=(*}
    rest=${declaration#*=(}
    while [[ $rest =~ $item_pattern ]]; do
      items+=("[${BASH_REMATCH[1]}]=${BASH_REMATCH[2]}")
      rest=${BASH_REMATCH[3]}
    done
    if [[ $rest == ')' && ${#items[@]} -gt 0 ]]; then
      sorted=$(printf '%s\n' "${items[@]}" | LC_ALL=C sort)
      joined=
      while read -r line; do
        joined+="$line "
      done <<< "$sorted"
      declaration="$head=($joined)"
    fi
  fi
  printf '[%s]\n' "$declaration"
}

reads=('$ref' '${ref}' '${ref[@]}' '${ref[*]}' '${ref[0]}' '${ref[key]}'
  '${#ref}' '${#ref[@]}' '${!ref}' '${!ref[@]}' '${ref:-dflt}' '${ref:+alt}'
  '${ref^^}' '${ref/o/0}' '${ref:1}' '${ref@Q}' '${ref[@]@Q}' '${ref@a}'
  '$((ref + 1))')
contexts=(
  "printf '<%s>' @E@; echo"
  "printf '<%s>' \"@E@\"; echo"
  "v=@E@; printf '<%s>\n' \"\$v\""
  "v=(@E@); printf '<%s>' \"\${v[@]}\"; echo"
  "for i in @E@; do printf '<%s>' \"\$i\"; done; echo"
  "[[ @E@ == \"@E@\" ]]; echo \"cond=\$?\""
  "case @E@ in '') echo empty;; *) echo word;; esac"
  "cat <<< \"@E@\""
  "f() { printf '<%s>' \"@E@\"; echo; }; f"
  "( printf '<%s>' \"@E@\"; echo )"
  "printf '<%s>' \"@E@\" | cat; echo"
)
writes=('ref=new' 'ref+=more' 'ref=(x y)' 'ref+=(z)' 'ref[1]=elem'
  'ref[2]=k' '((ref = 4 * 2))' '((ref++))' 'let ref+=3' 'read -r ref <<< rd'
  'read -ra ref <<< "r1 r2"' 'printf -v ref %s pv' 'mapfile -t ref <<< $'"'"'m1\nm2'"'"
  ': ${ref:=assigned}' 'declare ref=declared' 'export ref=exported'
  'for ref in l1 l2; do :; done' 'unset ref' 'unset "ref[1]"' 'OPTIND=1; getopts ab ref -b'
  'f() { local -n lr=ref; lr=via_local; }; f'
  'f() { local -n lr=target; lr=via_name; }; f')
write_contexts=(
  "@E@; show_target"
  "( @E@; show_target )"
  "eval '@E@'; show_target"
  "f2() { @E@; }; f2; show_target"
)

extra=read
for values in scalar unset_target empty_target element array sparse assoc \
  chain integer; do
  echo "== $values"
  for operator in "${reads[@]}"; do
    for context in "${!contexts[@]}"; do
      load_values "$values"
      run_case "${contexts[context]//@E@/$operator}"
    done
  done
done

extra=write
for values in scalar unset_target empty_target element array assoc chain \
  integer; do
  echo "== write $values"
  for operator in "${writes[@]}"; do
    for context in "${!write_contexts[@]}"; do
      [[ $context == 2 && $operator == *"'"* ]] && continue
      load_values "$values"
      run_case "${write_contexts[context]//@E@/$operator}"
    done
  done
done
