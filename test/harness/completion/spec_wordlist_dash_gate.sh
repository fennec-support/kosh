# A -W word list offers its dash entries only when the token already starts
# with a dash, the empty argument token completes the plain words, and a list
# reduced to nothing by the gate falls through to the filesystem. A function
# spec that replies nothing, returns status 124, or replies only options for an
# operand never claims the completion, so the operand falls to the filesystem.
# The dash token still consults the spec's options.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
touch "$dir/datafile"
cd "$dir"
echo "== empty token, mixed list:"
"$BIN" -c 'complete -W "alpha beta -x --why" mycmd' --debug-complete-at 'mycmd ' </dev/null
echo "== dash token, mixed list:"
"$BIN" -c 'complete -W "alpha beta -x --why" mycmd' --debug-complete-at 'mycmd -' </dev/null
echo "== empty token, flags-only list falls to files:"
"$BIN" -c 'complete -W "-x --why" flagcmd' --debug-complete-at 'flagcmd ' </dev/null
echo "== progcomp off ignores the registered specification:"
"$BIN" -c 'complete -W alpha mycmd; shopt -u progcomp' --debug-complete-at 'mycmd a' </dev/null
echo "== progcomp_alias off ignores the aliased specification:"
"$BIN" -c 'alias surface=target; complete -W alpha target' --debug-complete-at 'surface a' </dev/null
echo "== empty COMPREPLY falls to files:"
"$BIN" -c "_f(){ COMPREPLY=(); }; complete -F _f cp" --debug-complete-at 'cp data' </dev/null
echo "== retry status 124 falls to files:"
"$BIN" -c "_f(){ return 124; }; complete -F _f cp" --debug-complete-at 'cp data' </dev/null
echo "== dash-only reply on an operand falls to files:"
"$BIN" -c "_f(){ COMPREPLY=(--archive); }; complete -F _f cp" --debug-complete-at 'cp data' </dev/null
echo "== the dash token keeps the spec options:"
"$BIN" -c "_f(){ COMPREPLY=(--archive --backup); }; complete -F _f cp" --debug-complete-at 'cp --ar' </dev/null
