unset KOSH_FLAGS
# slurp takes exactly one file, and -e and -v cannot be combined. The checks
# run before slurp looks for a terminal.
for command in 'slurp' 'slurp a b' 'slurp -e -v a' 'slurp --emacs --vi a' 'slurp --mode vi a'
do
  echo "== $command:"
  "$BIN" -c "$command" 2>&1
  echo "rc=$?"
done
