unset KOSH_FLAGS
root=$TEST_TEMP_DIRECTORY/koshkit-input-is-output
mkdir -p "$root"
trap 'cd / && [ -n "$root" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$root"' EXIT
cd "$root" || exit 1

# split and uniq refuse an output name that is their input, cat refuses to
# read the regular file it writes to unless that file is empty, sort -o and
# uniq report a failed write instead of succeeding, and head leaves the rest
# of a seekable standard input for the next reader.

echo "--- split ---"
printf '1\n2\n3\n4\n' > xab
"$BIN" -c 'koshkit split -l 1 xab' 2> /dev/null
echo "status=$?"
"$BIN_DIR/invoke-koshkit" tr '\n' , < xab
echo

echo "--- cat ---"
printf 'one\n' > f
"$BIN" -c 'koshkit cat < f >> f' 2> /dev/null
echo "stdin=$? lines=$("$BIN_DIR/invoke-koshkit" wc -l < f | "$BIN_DIR/invoke-koshkit" tr -d ' ')"
"$BIN" -c 'koshkit cat f >> f' 2> /dev/null
echo "operand=$? lines=$("$BIN_DIR/invoke-koshkit" wc -l < f | "$BIN_DIR/invoke-koshkit" tr -d ' ')"
: > empty
"$BIN" -c 'koshkit cat empty >> empty'
echo "empty=$?"
printf 'two\n' > g
"$BIN" -c 'koshkit cat g f > h'
echo "other=$? lines=$("$BIN_DIR/invoke-koshkit" wc -l < h | "$BIN_DIR/invoke-koshkit" tr -d ' ')"

echo "--- sort and uniq ---"
printf 'b\na\n' > s
"$BIN" -c 'koshkit sort -o s s'
"$BIN_DIR/invoke-koshkit" tr '\n' , < s
echo
if [ -c /dev/full ] && [ "${OS-}" != Windows_NT ]; then
  "$BIN" -c 'printf "b\na\n" | koshkit sort -o /dev/full' 2> /dev/null
  echo "sort-full=$?"
  printf 'a\na\n' > u
  "$BIN" -c 'koshkit uniq u /dev/full' 2> /dev/null
  echo "uniq-full=$?"
  "$BIN" -c 'koshkit yes > /dev/full' 2> /dev/null
  echo "yes-full=$?"
else
  printf 'sort-full=2\nuniq-full=1\nyes-full=1\n'
fi

echo "--- uniq output ---"
printf 'a\na\nb\n' > same
"$BIN" -c 'koshkit uniq same same' 2> /dev/null
echo "same=$? kept=$("$BIN_DIR/invoke-koshkit" wc -l < same | "$BIN_DIR/invoke-koshkit" tr -d ' ')"

echo "--- head ---"
printf '1\n2\n3\n4\n5\n' > s5
"$BIN" -c '(koshkit head -n 2; koshkit cat) < s5'
"$BIN" -c '(read -r line; koshkit head -n 1; koshkit cat) < s5'
"$BIN" -c '(koshkit head -c 3; echo "|"; koshkit cat) < s5'
