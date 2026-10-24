# A cobra-style completion function appends "  (description)" to each value for
# its own menu. kosh renders its own dimmed description column, so the value is
# split from the description and only the bare value lands in the candidate
# list. The driver prints candidates alone, so a stripped value here proves the
# split ran. A value that carries no description passes through unchanged, and a
# value holding a parenthesis with no leading space is left whole.
echo "== cobra-style values split off the description:"
"$BIN" -c "_f(){ COMPREPLY=('void.ts.net  (the void node)' 'dupa.ts.net  (dupa node)'); }; complete -F _f ts" --debug-complete-at 'ts ' </dev/null
echo "== a value with no description is unchanged:"
"$BIN" -c "_f(){ COMPREPLY=('plainvalue'); }; complete -F _f tc" --debug-complete-at 'tc ' </dev/null
echo "== a parenthesis inside a value is left whole:"
"$BIN" -c "_f(){ COMPREPLY=('file(1).txt'); }; complete -F _f tp" --debug-complete-at 'tp ' </dev/null
# bash-completion ends a value with a space for readline to insert. The editor
# adds its own space, so the trailing spaces are trimmed before quoting.
echo "== trailing spaces are trimmed from a value:"
"$BIN" -c "_f(){ COMPREPLY=('clean ' 'checkout '); }; complete -F _f tg" --debug-complete-at 'tg c' </dev/null
# bash empties COMPREPLY before each completion, so a function that appends with
# COMPREPLY+=() does not inherit the entries of a previous completion. A stale
# COMPREPLY set before the run stands in for it.
echo "== a stale COMPREPLY does not leak into an appending spec:"
"$BIN" -c "COMPREPLY=(stale_one stale_two); _f(){ COMPREPLY+=(search); }; complete -F _f gn" --debug-complete-at 'gn sea' </dev/null
# A spec may return the same value many times, the way a wrapped
# bash-completion helper does when it cannot narrow the current word. Repeats
# collapse to one candidate, adjacent or not.
echo "== duplicates among distinct values are each collapsed:"
"$BIN" -c "_f(){ COMPREPLY=(build search profile search flake profile search); }; complete -F _f gn" --debug-complete-at 'gn ' </dev/null
