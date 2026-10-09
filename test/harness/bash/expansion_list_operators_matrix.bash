#!/bin/bash
# Every parameter operator applied to a whole array, ${a[@]OP} and
# ${a[*]OP}, in each word context of a script, checked against bash. The
# value sets cover an unset array, an empty one, empty elements, a sparse
# array, spaces, glob characters, quotes, newlines, control bytes, several
# IFS values, and nounset. Under an empty IFS, bash leaks its internal \001
# byte before each space that a trimmed element brings into an unquoted
# result, and Kosh does not. The leak cases drop that byte from their output
# in both shells before it is compared. In the [[ ]] context the leaked byte
# makes bash compare unequal words, so that case is not run here, and
# list_trim_empty_ifs_condition.kosh holds Kosh's answer.
export LC_ALL=C
shopt -s extglob
leak_cases=(
  'spaces|#?|@|0|ifs_empty'
  'spaces|#?|@|2|ifs_empty'
  'spaces|#?|@|4|ifs_empty'
  'spaces|#?|@|6|ifs_empty'
  'spaces|#?|@|7|ifs_empty'
  'spaces|#?|@|13|ifs_empty'
  'spaces|#?|*|0|ifs_empty'
  'spaces|#?|*|4|ifs_empty'
  'spaces|#?|*|6|ifs_empty'
  'spaces|#?|*|7|ifs_empty'
)
cd "$(mktemp -d)" || exit 1
error_file=$PWD/.error

load_values() {
  unset arr
  case $1 in
    unset) ;;
    empty) arr=() ;;
    one_empty) arr=('') ;;
    two_empty) arr=('' '') ;;
    sparse) arr=([1]=one [5]='fi ve') ;;
    spaces) arr=('a b' ' lead' 'trail ' 'x') ;;
    glob) arr=('*' '?x' '[ab]' 'a*b') ;;
    quotes) arr=("it's" 'say "hi"' 'back\slash' '$HOME') ;;
    newline) arr=($'l1\nl2' $'\n' 'x') ;;
    control) arr=($'a\tb' $'\002\033' 'Mixed Case') ;;
  esac
}

declare -A leak_set=()
for leak_key in "${leak_cases[@]}"; do
  leak_set[$leak_key]=1
done

run_case() {
  local case_key="$values|$operator|$subscript|$context|$extra"
  local status
  [[ $case_key == 'spaces|#?|@|9|ifs_empty' ]] && return 0
  printf '%s: ' "$1"
  if [[ -n ${leak_set[$case_key]-} ]]; then
    eval "$1" 2>"$error_file" | tr -d '\001'
    status=${PIPESTATUS[0]}
  else
    eval "$1" 2>"$error_file"
    status=$?
  fi
  if [ -s "$error_file" ]; then
    printf ' status=%s error\n' "$status"
  else
    printf ' status=%s\n' "$status"
  fi
}

operators=('' ':-d' '-d' ':+p' '+p' '#?' '##*' '#x' '%?' '%%?' '%e' '/a/A'
  '//a/A' '/#?/<' '/%?/>' '//[ab]/-' '/?/' '^' '^^' ',' ',,' '~' '~~' '^^[ab]'
  ':1' ':1:1' ': -1' ': -2:1' ':0:0' ': -9')
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
  "[[ -n @E@ ]]; echo \"nonempty=\$?\""
  "case @E@ in '') echo empty;; *) echo word;; esac"
  "case \"@E@\" in '') echo empty;; *) echo word;; esac"
  "cat <<< @E@"
  "cat <<< \"@E@\""
)

extra=
for values in unset empty one_empty two_empty sparse spaces glob quotes \
  newline control; do
  load_values "$values"
  echo "== $values"
  for operator in "${operators[@]}"; do
    for subscript in @ '*'; do
      expression="\${arr[$subscript]$operator}"
      for context in "${!contexts[@]}"; do
        run_case "${contexts[context]//@E@/$expression}"
      done
    done
  done
done

for extra in ifs_unset ifs_empty ifs_colon ifs_space_colon; do
  case $extra in
    ifs_unset) unset IFS ;;
    ifs_empty) IFS= ;;
    ifs_colon) IFS=: ;;
    ifs_space_colon) IFS=' :' ;;
  esac
  for values in two_empty spaces glob newline; do
    load_values "$values"
    echo "== $extra $values"
    for operator in '' ':-d' '/a/A' '^^' ':1' '#?'; do
      for subscript in @ '*'; do
        expression="\${arr[$subscript]$operator}"
        for context in "${!contexts[@]}"; do
          run_case "${contexts[context]//@E@/$expression}"
        done
      done
    done
  done
done
unset IFS

extra=nounset
for values in unset empty one_empty sparse; do
  load_values "$values"
  echo "== $extra $values"
  for operator in '' ':-d' '-d' ':+p' '+p' '#x' '/a/A' '^^' ':1'; do
    for subscript in @ '*'; do
      expression="\${arr[$subscript]$operator}"
      for context in 0 1 2 6 9 13; do
        run_case "( set -u; ${contexts[context]//@E@/$expression} )"
      done
    done
  done
done
