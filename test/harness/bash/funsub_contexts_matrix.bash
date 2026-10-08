#!/bin/bash
# Function substitutions, ${ cmd; } and ${| cmd; }, in every word and command
# context, checked against bash. The bodies print nothing, spaces, glob
# characters, quotes, and trailing newlines, change variables in the current
# shell, fail, and nest.
export LC_ALL=C
cd "$(mktemp -d)" || exit 1
error_file=$PWD/.error
script_file=$PWD/.sourced

run_case() {
  printf '%s: ' "$1"
  side=before
  eval "$1" 2>"$error_file"
  local status=$?
  if [ -s "$error_file" ]; then
    printf ' status=%s side=%s error\n' "$status" "$side"
  else
    printf ' status=%s side=%s\n' "$status" "$side"
  fi
}

bodies=(
  '${ echo out; }'
  '${ :; }'
  '${ printf "a  b"; }'
  '${ printf "%s\n" "*" "?"; }'
  '${ printf "it'"'"'s\n\n\n"; }'
  '${ side=changed; echo s; }'
  '${ false; }'
  '${ echo ${ echo nested; }; }'
  '${ local_value=1; echo "$local_value"; }'
  '${| REPLY=reply; }'
  '${| REPLY="a  b"; }'
  '${| side=changed; REPLY=r; }'
  '${| echo printed; }'
  '${| :; }'
)
contexts=(
  "printf '<%s>' @E@; echo"
  "printf '<%s>' \"@E@\"; echo"
  "v=@E@; printf '<%s>\n' \"\$v\""
  "v=(@E@); printf '<%s>' \"\${v[@]}\"; echo"
  "declare d=@E@; printf '<%s>\n' \"\$d\""
  "for i in @E@; do printf '<%s>' \"\$i\"; done; echo"
  "printf '<%s>' \"\${u:-@E@}\"; echo"
  "[[ -n @E@ ]]; echo \"cond=\$?\""
  "case @E@ in out) echo out;; *) echo other;; esac"
  "cat <<< \"@E@\""
  "cat <<EOF
<@E@>
EOF"
  "cat <<'EOF'
<@E@>
EOF"
  "p='<@E@>'; printf '%s\n' \"\${p@P}\""
  "echo \$(( \${#u} + 1 )) @E@ | cat"
  "f() { printf '<%s>' \"@E@\"; echo; }; f"
  "( printf '<%s>' \"@E@\"; echo )"
  "{ printf '<%s>' \"@E@\"; echo; } & wait"
  "printf '<%s>' \"@E@\" | cat; echo"
  "printf '%s\n' 'printf \"<%s>\" @E@; echo' > \"\$script_file\"; . \"\$script_file\""
  "trap 'printf \"<%s>\" @E@; echo' USR1; kill -USR1 \$\$; trap - USR1"
  "echo hi > \"x@E@\"; printf '[%s]' x*; echo; rm -f x*"
  "eval 'printf \"<%s>\" \"@E@\"; echo'"
  "echo \"\$( printf '<%s>' \"@E@\" )\""
)

for operator in "${bodies[@]}"; do
  for context in "${!contexts[@]}"; do
    case $context in
      18|19|21)
        [[ $operator == *"'"* ]] && continue
        ;;
    esac
    run_case "${contexts[context]//@E@/$operator}"
  done
done

for operator in '${ echo "$unset_name"; }' '${| REPLY=$unset_name; }' \
  '${ echo ok; }'; do
  for context in 0 1 2; do
    run_case "( set -u; ${contexts[context]//@E@/$operator} )"
  done
done
