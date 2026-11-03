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
"$BIN" --debug-complete-at 'mycmd ' -c 'complete -W "alpha beta -x --why" mycmd' </dev/null
echo "== dash token, mixed list:"
"$BIN" --debug-complete-at 'mycmd -' -c 'complete -W "alpha beta -x --why" mycmd' </dev/null
echo "== empty token, flags-only list falls to files:"
"$BIN" --debug-complete-at 'flagcmd ' -c 'complete -W "-x --why" flagcmd' </dev/null
echo "== progcomp off ignores the registered specification:"
"$BIN" --debug-complete-at 'mycmd a' -c 'complete -W alpha mycmd; shopt -u progcomp' </dev/null
echo "== progcomp_alias off ignores the aliased specification:"
"$BIN" --debug-complete-at 'surface a' -c 'alias surface=target; complete -W alpha target' </dev/null
echo "== empty COMPREPLY falls to files:"
"$BIN" --debug-complete-at 'cp data' -c "_f(){ COMPREPLY=(); }; complete -F _f cp" </dev/null
echo "== retry status 124 falls to files:"
"$BIN" --debug-complete-at 'cp data' -c "_f(){ return 124; }; complete -F _f cp" </dev/null
echo "== dash-only reply on an operand falls to files:"
"$BIN" --debug-complete-at 'cp data' -c "_f(){ COMPREPLY=(--archive); }; complete -F _f cp" </dev/null
echo "== the dash token keeps the spec options:"
"$BIN" --debug-complete-at 'cp --ar' -c "_f(){ COMPREPLY=(--archive --backup); }; complete -F _f cp" </dev/null
