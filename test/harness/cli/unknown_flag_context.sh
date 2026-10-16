unset KOSH_FLAGS
# Unknown flag errors carry the program context. A builtin reports the
# 'Builtin name' prefix and a koshkit utility reports the 'koshkit util'
# prefix, with the caret staying on the flag.
echo "== builtin unknown flag carries the Builtin prefix:"
"$BIN" -c 'enable --badflag' 2>&1
[ "$?" -eq 2 ] || exit 1
echo "== koshkit unknown flag carries the koshkit util prefix:"
"$BIN" -c 'koshkit ls --dasdas' 2>&1
[ "$?" -eq 2 ] || exit 1
echo "== multibyte unknown flag in a koshkit utility keeps the whole character:"
"$BIN" -c 'koshkit eviliso -в' 2>&1
[ "$?" -eq 2 ] || exit 1
echo "== multibyte unknown flag after an ASCII flag keeps the whole character:"
"$BIN" -c 'koshkit ls -aв' 2>&1
[ "$?" -eq 2 ] || exit 1
echo "== multibyte unknown flag in a builtin keeps the whole character:"
"$BIN" -c 'type -в' 2>&1
[ "$?" -eq 2 ] || exit 1
echo "== multibyte unknown option in set keeps the whole character:"
"$BIN" -c 'set -aв' 2>&1
[ "$?" -eq 1 ] || exit 1
echo "== multibyte unknown flag of the shell keeps the whole character:"
"$BIN" -в 2>&1 | grep -o "error: Unknown flag .*"
[ "${PIPESTATUS[0]}" -eq 2 ] || exit 1
echo "== a long flag matches its whole name, not a prefix:"
output=$("$BIN" --helpXYZ -c 'echo unreached' 2>&1)
[ "$?" -eq 2 ] || exit 1
if printf '%s\n' "$output" | grep -qx 'unreached'; then
  exit 1
fi
printf '%s\n' "$output" | grep -o "error: Unknown flag .*"
echo "== a shell flag close to a known flag suggests it:"
for typed in --mimicry --enable-mimicy --zz-bogus-flag; do
  echo "== $typed"
  out=$("$BIN" "$typed" 2>&1)
  [ "$?" -eq 2 ] || exit 1
  printf '%s\n' "$out" | grep -E "^note:"
done
echo "== multibyte invalid option in exec and eval keeps the whole character:"
"$BIN" -c 'exec -в' 2>&1
[ "$?" -eq 2 ] || exit 1
"$BIN" -c 'eval -в' 2>&1
[ "$?" -eq 2 ]
