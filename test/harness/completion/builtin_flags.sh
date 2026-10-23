# The builtin flag stage loads the registered FLAG lists, the set option
# table, the shopt options, kill's signals, and the shell binary's own
# flags, all without a manpage.
echo "== set switches:"
"$BIN" --debug-complete-at 'set -' </dev/null
echo "== set -o options by prefix:"
"$BIN" --debug-complete-at 'set -o no' </dev/null
echo "== set -o offers no Koshka name:"
"$BIN" --debug-complete-at 'set -o annoying-' </dev/null
echo "== kosh no-traces flag:"
"$BIN" --debug-complete-at 'kosh --no-t' </dev/null
echo "== kosh no-annoying flag:"
"$BIN" --debug-complete-at 'kosh --no-a' </dev/null
echo "== declare letters:"
"$BIN" --debug-complete-at 'declare -' </dev/null
echo "== kill signals:"
signal_names=$("$BIN" --debug-complete-at 'kill -' </dev/null)
printf '%s\n' "$signal_names" | grep -E '^-(KILL|TERM)$'
if [ "${OS-}" != Windows_NT ]; then
  for signal_name in HUP INT QUIT; do
    printf '%s\n' "$signal_names" | grep -q "^-$signal_name$" ||
      printf 'missing-signal=%s\n' "$signal_name"
  done
  for signal_name in ABRT ALRM CONT PIPE STOP TSTP USR1 USR2; do
    printf '%s\n' "$signal_names" | grep -q "^-$signal_name$" ||
      printf 'missing-signal=%s\n' "$signal_name"
  done
fi
echo "== shopt options by prefix:"
"$BIN" --debug-complete-at 'shopt glob' </dev/null
echo "== read flags:"
"$BIN" --debug-complete-at 'read -' </dev/null
echo "== fc flags:"
"$BIN" --debug-complete-at 'fc -' </dev/null
echo "== time flags:"
"$BIN" --debug-complete-at 'builtin time -' </dev/null
