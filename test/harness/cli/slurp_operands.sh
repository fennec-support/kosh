unset KOSH_FLAGS
# A +LINE operand needs a file after it and only one key set can be given.
# These checks precede terminal setup; named-file editing requires tty output.
for command in 'slurp +3' 'slurp a +3' 'slurp -e -v a' \
  'slurp --emacs --vi a' 'slurp -v -V a' 'slurp --mode vi a' 'slurp a b' \
  'koshkit vi -e a'
do
  echo "== $command:"
  "$BIN" -c "$command" 2>&1
  echo "rc=$?"
done
