unset KOSH_FLAGS
# A runtime diagnostic renders the same in every mood: the file, line, column,
# source line, caret, note, and trace frames. Each script fails once, at its
# last command, so the statuses of the moods stay out of the comparison. The
# default mood output is the reference and any other mood prints its difference.
d=$(mktemp -d)
trap '[ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT
cd "$d" || exit 1

compare()
{
    name=$1
    "$BIN" --mood kosh "$name" > reference.out 2>&1
    for mood in bash sh bash-posix; do
        "$BIN" --mood "$mood" "$name" > mood.out 2>&1
        if diff reference.out mood.out > mood.diff 2>&1; then
            printf '%s %s: same\n' "$name" "$mood"
        else
            printf '%s %s: differs\n' "$name" "$mood"
            cat mood.diff
        fi
    done
    "$BIN" --dumb "$name" > mood.out 2>&1
    if diff reference.out mood.out > mood.diff 2>&1; then
        printf '%s dumb: same\n' "$name"
    else
        printf '%s dumb: differs\n' "$name"
        cat mood.diff
    fi
    printf -- '-- reference\n'
    sed "s|$d|DIR|g" reference.out
}

printf 'echo before\neval '"'"'echo $((1/0))'"'"'\n' > eval.sh
compare eval.sh

printf 'cat <<EOT\nvalue $((1/0))\nEOT\n' > heredoc.sh
compare heredoc.sh

printf 'f()\n{\n  echo $((1/0))\n}\nf\n' > function.sh
compare function.sh

printf 'echo lib\necho $((1/0))\n' > lib.sh
printf 'echo start\n. ./lib.sh\n' > source.sh
compare source.sh

printf 'trap '"'"'echo $((1/0))'"'"' EXIT\necho body\n' > trap.sh
compare trap.sh

printf 'echo before\n( echo $((1/0)) )\n' > subshell.sh
compare subshell.sh

printf 'value=$(echo $((1/0)))\n' > substitution.sh
compare substitution.sh

printf 'cat <(echo $((1/0)))\n' > process.sh
compare process.sh

printf 'ref=undefined_name\nset -u\necho "${!ref}"\n' > unbound.sh
compare unbound.sh

printf 'readonly fixed=1\nfixed=2\n' > readonly.sh
compare readonly.sh

printf 'cd /no/such/directory_zz\n' > cd.sh
compare cd.sh

printf '#!/bin/bash\nfiles=$(echo *.nomatch)\n' > glob.sh
"$BIN" --mood kosh -WWW glob.sh > reference.out 2>&1
for mood in bash sh bash-posix; do
    "$BIN" --mood "$mood" -WWW glob.sh > mood.out 2>&1
    if diff reference.out mood.out > mood.diff 2>&1; then
        printf 'glob.sh %s: same\n' "$mood"
    else
        printf 'glob.sh %s: differs\n' "$mood"
        cat mood.diff
    fi
done
printf -- '-- reference\n'
cat reference.out
