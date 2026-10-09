#!/bin/bash
# Process substitutions, <( ) and >( ), in every word and command context,
# checked against bash. Each case reads the substituted path back or checks
# its shape, so no descriptor number reaches the output. The bodies print
# nothing, several lines, spaces, glob characters, fail, and nest. A wait on
# $! after a pipeline stage's substitution must not hang. That substitution
# belongs to the stage's subshell, so neither shell waits for it, and the case
# polls for its output before reading it.
export LC_ALL=C
cd "$(mktemp -d)" || exit 1
error_file=$PWD/.error
script_file=$PWD/.sourced

run_case() {
  printf '%s: ' "$1"
  eval "$1" 2>"$error_file"
  local status=$?
  if [ -s "$error_file" ]; then
    printf ' status=%s error\n' "$status"
  else
    printf ' status=%s\n' "$status"
  fi
}

show_path() {
  local path
  for path; do
    if [[ $path == /dev/fd/* || $path == /proc/* || $path == /tmp/* ]]; then
      printf '[path:'
      cat -- "$path"
      printf ']'
    else
      printf '[word:%s]' "$path"
    fi
  done
  echo
}

bodies=(
  'echo one'
  'printf "%s\n" a "b c" "*"'
  ':'
  'false'
  'cat < <(echo inner)'
  'printf "x  y"'
)
readers=(
  "show_path <(@E@)"
  "cat <(@E@)"
  "v=<(@E@); show_path \"\$v\""
  "v=(<(@E@) <(echo second)); show_path \"\${v[@]}\""
  "declare d=<(@E@); show_path \"\$d\""
  "for p in <(@E@); do show_path \"\$p\"; done"
  "show_path \${u:-<(@E@)}"
  "show_path \"\${u:-<(@E@)}\""
  "w=pre<(@E@)post; [[ \$w == pre/*post ]] && echo shape-ok"
  "while read -r line; do echo \"line:\$line\"; done < <(@E@)"
  "cat < <(@E@) | cat"
  "f() { cat <(@E@); }; f"
  "( cat <(@E@) )"
  "{ cat <(@E@); } & wait"
  "echo \"\$(cat <(@E@))\""
  "eval 'cat <(@E@)'"
  "printf '%s\n' 'cat <(@E@)' > \"\$script_file\"; . \"\$script_file\""
  "case <(@E@) in /*) echo absolute;; *) echo other;; esac"
  "[[ -r <(@E@) ]]; echo \"readable=\$?\""
  "exec 7< <(@E@); cat <&7; exec 7<&-"
  "paste <(@E@) <(@E@)"
)
writers=(
  "echo data > >(@W@ > out.txt); wait \$!; cat out.txt"
  "printf 'l1\nl2\n' | tee >(@W@ > out.txt) > /dev/null; wait \$!; for n in {1..50}; do [ -s out.txt ] && break; sleep 0.1; done; cat out.txt"
  "v=>(@W@ > out.txt); echo via-var > \"\$v\"; wait \$!; cat out.txt"
  "{ echo grouped; } > >(@W@ > out.txt); wait \$!; cat out.txt"
  "f() { echo func > >(@W@ > out.txt); wait \$!; }; f; cat out.txt"
)

for operator in "${bodies[@]}"; do
  for context in "${!readers[@]}"; do
    case $context in
      15|16)
        [[ $operator == *"'"* ]] && continue
        ;;
    esac
    run_case "${readers[context]//@E@/$operator}"
  done
done

for operator in 'cat' 'tr a-z A-Z' 'sort -r' 'wc -l'; do
  for context in "${!writers[@]}"; do
    rm -f out.txt
    run_case "${writers[context]//@W@/$operator}"
  done
done
