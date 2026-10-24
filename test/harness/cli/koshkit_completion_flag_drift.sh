unset KOSH_FLAGS
# The generated block of the bash completion file must match what
# scripts/generate-completions.sh builds from koshkit --list-flags, so every
# registered utility flag is completed. Only a difference is printed.
root=${BIN%/*}
completion=$root/completions/kosh.bash

if "$root/scripts/generate-completions.sh" "$BIN" "$completion" \
  | diff "$completion" -
then
  echo "ok: the generated completion block is current"
fi
