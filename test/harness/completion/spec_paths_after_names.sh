# A spec that offers both names and paths, as the man spec does with the
# entries of the working directory, lists its names first and the candidates
# spelled as paths after them, so the ./ entries do not crowd out the pages. A
# name that only holds a slash, such as a branch, sorts with the other names,
# and a spec that asks for nosort keeps its own order.
echo "== names before paths:"
"$BIN" -c "_f(){ COMPREPLY=(./docs/ zcat ./assets/ ls ../up /abs '~/home' lsblk); }; complete -F _f tm" \
  --debug-complete-at 'tm ' </dev/null
echo "== a name with a slash stays among the names:"
"$BIN" -c "_f(){ COMPREPLY=(origin/main main feature/x tag1); }; complete -F _f tb" \
  --debug-complete-at 'tb ' </dev/null
echo "== nosort keeps the reply order:"
"$BIN" -c "_f(){ COMPREPLY=(./docs/ zcat ls); }; complete -o nosort -F _f tn" \
  --debug-complete-at 'tn ' </dev/null
