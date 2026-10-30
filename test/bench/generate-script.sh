#!/bin/sh
# Writes a Bash script for the lint and format benchmarks to standard output.
# SCALE sets the number of generated blocks, so the script length and the
# analysis work grow in proportion. Each block mixes functions, arrays, loops,
# case arms, here-documents, substitutions, findings, and irregular layout.

set -u

# The work multiplier. Each unit adds twenty blocks of about thirty lines.
SCALE=${SCALE:-100}
BLOCK_COUNT=$((SCALE * 20))

printf '%s\n' '#!/usr/bin/env bash' 'set -u' '' 'declare -A registry=()' \
    'total=0' ''

block_index=0
while [ "$block_index" -lt "$BLOCK_COUNT" ]; do
    n=$block_index
    printf '%s\n' \
        "# Block $n." \
        "process_$n() {" \
        '  local input=$1 count=0' \
        "  local -a items=(alpha_$n beta_$n \"gamma \$input\")" \
        '    for item in "${items[@]}"; do' \
        '        if [[ $item == *a* && ${#item} -gt 3 ]]; then count=$((count + 1))' \
        '        elif [ "$item" = "beta" ] ; then' \
        '      echo "$item"' \
        '        else' \
        '            : "${item//a/b}" "${item%%_*}" "${item:-none}"' \
        '        fi' \
        '    done' \
        '    case $input in' \
        "    start*|begin) registry[\$input]=$n ;;" \
        '    stop) return 1;;' \
        '    *[0-9]*)' \
        '        while read -r line; do count=$((count+${#line})); done <<EOF' \
        "line one $n" \
        '$input' \
        'EOF' \
        '    ;;' \
        '    *) count=$(( count * 2 + $(printf %d "${#input}") )) ;;' \
        '    esac' \
        "  result_$n=\$(echo \"\$input\" | tr a-z A-Z | while read -r w; do printf '%s' \"\$w\"; done)" \
        "  { echo \"\$count\"; echo \"\${result_$n}\"; } >/dev/null 2>&1" \
        '  (( total += count ))' \
        '  [[ -n ${registry[$input]-} ]] && unset "registry[$input]"' \
        '  return 0' \
        '}' \
        "process_$n start_$n || echo failed $n >&2" \
        ''
    block_index=$((block_index + 1))
done

printf '%s\n' 'echo "$total"'
