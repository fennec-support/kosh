unset KOSH_FLAGS
# slurp takes at least one file, a +LINE operand needs a file after it, and
# only one of -e, -v, and -V can be given. The checks run before slurp looks
# for a terminal, and a file list without a terminal is refused.
for command in 'slurp' 'slurp +3' 'slurp a +3' 'slurp -e -v a' \
  'slurp --emacs --vi a' 'slurp -v -V a' 'slurp --mode vi a' 'slurp a b' \
  'koshkit slurp' 'koshkit vi -e a'
do
  echo "== $command:"
  "$BIN" -c "$command" 2>&1
  echo "rc=$?"
done
