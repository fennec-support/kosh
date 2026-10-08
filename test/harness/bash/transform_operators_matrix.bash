#!/bin/bash
# Every transformation operator, @Q, @A, @K, @k, @E, @P, @U, @u, @L, and @a,
# on scalars, indexed and associative arrays, and the positional parameters,
# in each word context of a script, checked against bash. The values cover
# unset and empty names, spaces, quotes, escapes, newlines, and control
# bytes. Cases listed in the pending fixture are skipped.
export LC_ALL=C
list_only=1
. "${BASH_SOURCE%/*}/transform_operators_matrix_pending.bash"
unset list_only
cd "$(mktemp -d)" || exit 1
error_file=$PWD/.error

load_values() {
  unset s arr map
  declare -gA map
  set_positional
  case $1 in
    unset)
      unset map
      ;;
    empty)
      s= arr=() ;;
    one_empty)
      s='' arr=('') map=([k]='')
      set_positional ''
      ;;
    spaces)
      s=' a  b ' arr=('a b' ' lead') map=(['x y']='v w')
      set_positional 'p 1' ' p2'
      ;;
    quotes)
      s="it's \"q\"" arr=("it's" '"dq"' 'b\s') map=(["k'1"]='v"1')
      set_positional "o'ne" '"two"'
      ;;
    escapes)
      s='a\tb\x41\101\e\\' arr=('\n' '\a\b' 'é') map=(['\t']='\x42')
      set_positional '\t' '\\'
      ;;
    newline)
      s=$'l1\nl2' arr=($'\n' 'x') map=([$'k\n']=$'v\n')
      set_positional $'n\nn'
      ;;
    control)
      s=$'\033[0m\t' arr=($'\002' $'a\tb') map=([$'\033']=$'\t')
      set_positional $'\033' $'\x7f'
      ;;
    prompt)
      s='\\ \[x\] \e \a \n \041' arr=('\\' 'x\]y') map=([p]='\\')
      set_positional '\\'
      ;;
    sparse)
      s=x arr=([3]=three [7]='se ven') map=([only]=1)
      set_positional a b c
      ;;
  esac
}

set_positional() {
  positional=("$@")
}

declare -A pending_set=()
for pending_key in "${pending_cases[@]}"; do
  pending_set[$pending_key]=1
done

run_case() {
  local case_key="$values|$operator|$target|$context|$extra"
  if [[ -n ${pending_set[$case_key]-} ]]; then
    [ -n "${is_pending_run-}" ] || return 0
  else
    [ -z "${is_pending_run-}" ] || return 0
  fi
  local body=$1
  printf '%s: ' "$body"
  set -- "${positional[@]}"
  eval "$body" 2>"$error_file"
  local status=$?
  if [ -s "$error_file" ]; then
    printf ' status=%s error\n' "$status"
  else
    printf ' status=%s\n' "$status"
  fi
}

operators=(Q A K k E P U u L a)
targets=('s' 'arr[@]' 'arr[*]' 'arr[1]' 'map[@]' 'map[*]' '@' '*' '1')
contexts=(
  "printf '<%s>' @E@; echo"
  "printf '<%s>' \"@E@\"; echo"
  "v=@E@; printf '<%s>\n' \"\$v\""
  "v=\"@E@\"; printf '<%s>\n' \"\$v\""
  "v=(@E@); printf '<%s>' \"\${v[@]}\"; echo"
  "v=(\"@E@\"); printf '<%s>' \"\${v[@]}\"; echo"
  "for i in @E@; do printf '<%s>' \"\$i\"; done; echo"
  "printf '<%s>' \${u:-@E@}; echo"
  "printf '<%s>' \"\${u:-@E@}\"; echo"
  "[[ @E@ == \"@E@\" ]]; echo \"cond=\$?\""
  "case @E@ in '') echo empty;; *) echo word;; esac"
  "cat <<< @E@"
  "cat <<< \"@E@\""
  "declare d=@E@; printf '<%s>\n' \"\$d\""
)

extra=
for values in unset empty one_empty spaces quotes escapes newline control \
  prompt sparse; do
  load_values "$values"
  echo "== $values"
  for operator in "${operators[@]}"; do
    for target in "${targets[@]}"; do
      expression="\${$target@$operator}"
      for context in "${!contexts[@]}"; do
        run_case "${contexts[context]//@E@/$expression}"
      done
    done
  done
done

extra=nounset
for values in unset empty one_empty; do
  load_values "$values"
  echo "== $extra $values"
  for operator in Q A K a E; do
    for target in "${targets[@]}"; do
      expression="\${$target@$operator}"
      for context in 0 1 2; do
        run_case "( set -u; ${contexts[context]//@E@/$expression} )"
      done
    done
  done
done
