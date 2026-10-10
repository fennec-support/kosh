#!/bin/sh
# Every POSIX parameter operator applied to $@, $*, $1, and a scalar in each
# word context, checked against dash. The values cover no parameters, empty
# parameters, spaces, glob characters, quotes, and newlines, under several
# IFS values. A case listed in the warnings file reports its output and
# status but not whether it wrote to stderr, because Kosh warns there about
# an unset variable where dash is silent. The for loop over two empty
# parameters under an empty IFS is left out: dash 0.5.13 loops once there,
# while dash 0.5.12, bash, yash, and busybox loop zero times.
LC_ALL=C
export LC_ALL
. "${0%/*}/positional_operators_matrix_warnings.sh"
cd "$(mktemp -d)" || exit 1
error_file=$PWD/.error
newline='
'
default_ifs=" 	$newline"

load_values() {
  case $1 in
    none) set -- ;;
    one_empty) set -- '' ;;
    two_empty) set -- '' '' ;;
    spaces) set -- 'a b' ' lead' 'trail ' ;;
    glob) set -- '*' '?x' '[ab]' ;;
    quotes) set -- "it's" 'say "hi"' 'back\slash' ;;
    newline) set -- "l1${newline}l2" "$newline" x ;;
    *) set -- ;;
  esac
  values_args=$(for argument in "$@"; do
    printf "'%s' " "$(printf '%s' "$argument" | sed "s/'/'\\\\''/g")"
  done)
  eval "warning_group=\${warning_${extra}_${values}-}"
}

run_case() {
  case_key="$operator|$target|$context"
  is_warning_case=
  case $warning_group in
    *"<$case_key>"*) is_warning_case=1 ;;
  esac
  printf '%s: ' "$1"
  IFS=$case_ifs
  eval "set -- $values_args; s=\$*; $1" 2>"$error_file"
  status=$?
  IFS=$default_ifs
  if [ -s "$error_file" ] && [ -z "$is_warning_case" ]; then
    printf ' status=%s error\n' "$status"
  else
    printf ' status=%s\n' "$status"
  fi
}

set_body() {
  case $1 in
    0) body="printf '<%s>' $2; echo" ;;
    1) body="printf '<%s>' \"$2\"; echo" ;;
    2) body="v=$2; printf '<%s>\n' \"\$v\"" ;;
    3) body="v=\"$2\"; printf '<%s>\n' \"\$v\"" ;;
    4) body="for i in $2; do printf '<%s>' \"\$i\"; done; echo" ;;
    5) body="printf '<%s>' \${u:-$2}; echo" ;;
    6) body="printf '<%s>' \"\${u:-$2}\"; echo" ;;
    7) body="case $2 in '') echo empty;; *) echo word;; esac" ;;
    8) body="case \"$2\" in '') echo empty;; *) echo word;; esac" ;;
    9) body="cat <<EOF${newline}<$2>${newline}EOF" ;;
    10) body="f() { printf '<%s>' $2; echo; }; f" ;;
    11) body="( printf '<%s>' \"$2\"; echo )" ;;
    12) body="v=\$(printf '<%s>' $2); printf '%s\n' \"\$v\"" ;;
    13) body="printf '<%s>' $2 | cat; echo" ;;
    *) body=: ;;
  esac
}

for extra in default empty colon; do
  case $extra in
    empty) case_ifs= ; contexts='0 1 2 4 7 9' ;;
    colon) case_ifs=: ; contexts='0 1 2 4 7 9' ;;
    *) case_ifs=$default_ifs ; contexts='0 1 2 3 4 5 6 7 8 9 10 11 12 13' ;;
  esac
  for values in none one_empty two_empty spaces glob quotes newline; do
    load_values "$values"
    echo "== $extra $values"
    for operator in '' ':-d' '-d' ':+p' '+p' '#?' '##*' '%?' '%%?' '#x'; do
      for target in '@' '*' '1' 's'; do
        expression="\${$target$operator}"
        for context in $contexts; do
          if [ "$extra $values $context" = "empty two_empty 4" ]; then
            continue
          fi
          set_body "$context" "$expression"
          run_case "$body"
        done
      done
    done
    for target in '#' '#1' '#s'; do
      operator=length
      expression="\${$target}"
      for context in 0 1 2; do
        set_body "$context" "$expression"
        run_case "$body"
      done
    done
  done
done
