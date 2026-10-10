# A spec that offers both names and paths, as the man spec does with the
# entries of the working directory, lists its names first and its paths after
# them, so the ./ entries do not crowd out the pages. A spec that asks for
# nosort keeps its own order.
echo "== names before paths:"
"$BIN" -c "_f(){ COMPREPLY=(./docs/ zcat ./assets/ ls lsblk); }; complete -F _f tm" \
  --debug-complete-at 'tm ' </dev/null
echo "== nosort keeps the reply order:"
"$BIN" -c "_f(){ COMPREPLY=(./docs/ zcat ls); }; complete -o nosort -F _f tn" \
  --debug-complete-at 'tn ' </dev/null
