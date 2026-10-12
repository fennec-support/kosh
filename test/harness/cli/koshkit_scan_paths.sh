# The koshkit utilities that scan whole words, runs, or tables run against
# fixed inputs in a temporary directory: cksum around its four-byte step,
# diff with several separated edits, find printing and running per match,
# pr padding columns, cut past the last selected byte, and grep -l stopping
# a matched file early.
unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

echo "--- cksum lengths ---"
for n in 0 1 2 3 4 5 7 8 9 4096 65537; do
  "$BIN" -c 'koshkit seq 100000 | koshkit head -c "$1"' head "$n" > "len-$n"
  "$BIN" -c 'koshkit cksum "$1"' cksum "len-$n"
done
echo "--- cksum stdin ---"
"$BIN" -c 'koshkit seq 3000 | koshkit cksum'

echo "--- diff separated edits ---"
"$BIN" -c 'koshkit seq 60' > left.txt
"$BIN" -c 'koshkit seq 60' | sed -e '7d' -e '19s/.*/nineteen/' -e '40a\
inserted' -e '55,56d' > right.txt
"$BIN" -c 'koshkit diff left.txt right.txt; printf "status=%s\n" "$?"'

echo "--- find print and exec per match ---"
"$BIN" -c 'koshkit mkdir -p tree/sub && koshkit touch tree/sub/leaf tree/top'
"$BIN" -c 'koshkit find tree -name leaf -print0 -print -exec echo exec {} ";"' | tr '\0' '|'
echo
"$BIN" -c 'koshkit find tree -type f -print0' | tr '\0' '\n' | sort

echo "--- pr columns ---"
"$BIN" -c 'koshkit seq 9 | koshkit pr -3 -t -w 30'
"$BIN" -c 'koshkit seq 4 | koshkit pr -2 -t -o 3 -w 20'

echo "--- cut past the last range ---"
printf 'abcdefghij\nxy\n\n0123456789abcdef\n' > cut.txt
"$BIN" -c 'koshkit cut -b 2-4,6 cut.txt'
"$BIN" -c 'koshkit cut -c -3 cut.txt'
printf 'a:b:c:d\ne\n' | "$BIN" -c 'koshkit cut -d: -f1,3'

echo "--- grep -l across files ---"
"$BIN" -c 'koshkit seq 200000' > big.txt
printf 'no match here\n' > none.txt
printf 'x\n7\n' > small.txt
"$BIN" -c 'koshkit grep -l 7 big.txt none.txt small.txt; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit grep -l zzz big.txt none.txt; printf "status=%s\n" "$?"'
