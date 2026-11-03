# Completion re-roots to the command line of the substitution the cursor sits
# in, so a token inside $(...), inside backticks, inside <(...), or inside the
# substitution of a compound head completes the inner command rather than the outer one. A
# registered spec and a controlled directory keep the candidates stable across
# machines.
dir=$(mktemp -d)
trap '[ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT

echo "== inside \$( ):"
"$BIN" --debug-complete-at 'echo $(probecmd a' -c 'complete -W "alpha beta gamma" probecmd' </dev/null

echo "== inside backticks:"
"$BIN" --debug-complete-at 'echo `probecmd b' -c 'complete -W "alpha beta gamma" probecmd' </dev/null
echo "== inside a for-head substitution:"
"$BIN" --debug-complete-at 'for x in $(probecmd g' -c 'complete -W "alpha beta gamma" probecmd' </dev/null
echo "== inside a process substitution:"
"$BIN" --debug-complete-at 'cat <(probecmd a' -c 'complete -W "alpha beta gamma" probecmd' </dev/null
echo "== inside a process substitution in an assignment value:"
"$BIN" --debug-complete-at 'p=x<(probecmd b' -c 'complete -W "alpha beta gamma" probecmd' </dev/null
echo "== filesystem still completes inside a substitution:"
: > "$dir/onlyfile"
filesystem_result=$("$BIN" \
    --debug-complete-at "echo \$(cat $dir/only" </dev/null)
test "$filesystem_result" = "(candidate \"$dir/onlyfile\")"
echo onlyfile
echo "== arithmetic is not a command body:"
(
    cd "$dir" &&
        "$BIN" --debug-complete-at 'echo $((probecmd a' \
            -c 'complete -W "alpha beta gamma" probecmd' </dev/null
)
