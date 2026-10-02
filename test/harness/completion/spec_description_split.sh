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
# A function that calls compopt -o filenames gets a slash after each directory,
# for both a command spec and the -D loader. Without the option a directory
# name is left bare, as in bash.
echo "== compopt -o filenames marks directories:"
"$BIN" -c "_f(){ compopt -o filenames; COMPREPLY=(harness Makefile); }; complete -F _f tl" --debug-complete-at 'tl ' </dev/null
echo "== the -D loader marks directories after compopt -o filenames:"
"$BIN" -c "_d(){ compopt -o filenames; COMPREPLY=(harness Makefile); }; complete -D -F _d" --debug-complete-at 'td ' </dev/null
echo "== a directory is left bare without compopt -o filenames:"
"$BIN" -c "_f(){ COMPREPLY=(harness); }; complete -F _f tb" --debug-complete-at 'tb ' </dev/null
