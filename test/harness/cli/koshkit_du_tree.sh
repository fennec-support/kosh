# The du tree and top-largest listings run inside a fresh temporary directory.
# Allocated sizes depend on the filesystem, so tree lines are compared with the
# size column masked, and the sizes themselves are checked against the flat
# listing from the same run.
unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

mask_sizes() {
  sed 's/^ *[0-9][0-9.]*[KMGTP]* //'
}

make_file() {
  dd if=/dev/zero of="$1" bs=1000 count="$2" 2>/dev/null
}

mkdir -p t/a/b t/c
make_file t/a/b/big 200
make_file t/a/mid 50
make_file t/c/small 8
make_file t/top 20
make_file t/x1 1
make_file t/x2 1

echo "--- tree ---"
"$BIN" -c 'koshkit du --tree t' | mask_sizes
echo "--- tree sizes match the flat listing ---"
flat_total=$("$BIN" -c 'koshkit du -s t' | cut -f 1 -d ' ')
tree_total=$("$BIN" -c 'koshkit du --tree t' | head -n 1 | sed 's/^ *//' |
  cut -f 1 -d ' ')
if [ "$flat_total" = "$tree_total" ]; then
  echo "tree-root-size=matched"
else
  echo "tree-root-size=wrong"
fi
flat_big=$("$BIN" -c 'koshkit du t/a/b/big' | cut -f 1 -d ' ')
tree_big=$("$BIN" -c 'koshkit du --tree t' | grep ' big$' | sed 's/^ *//' |
  cut -f 1 -d ' ')
if [ "$flat_big" = "$tree_big" ]; then
  echo "tree-leaf-size=matched"
else
  echo "tree-leaf-size=wrong"
fi
echo "--- tree human sizes ---"
"$BIN" -c 'koshkit du -h --tree t' | mask_sizes
echo "--- tree summary ---"
"$BIN" -c 'koshkit du --tree -s t t/c' | mask_sizes
echo "--- tree operands ---"
"$BIN" -c 'koshkit du --tree t/a' | mask_sizes
(cd t && "$BIN" -c 'koshkit du --tree .') | mask_sizes
(cd t && "$BIN" -c 'koshkit du --tree ./a/') | mask_sizes
echo "--- top-largest ---"
"$BIN" -c 'koshkit du -T t' | mask_sizes
if "$BIN" -c 'koshkit du -T t' | head -n 1 | grep -q '[KMG] '; then
  echo "top-human=yes"
else
  echo "top-human=no"
fi
"$BIN" -c 'koshkit du --top-largest -s t' | mask_sizes

mkdir -p m/deep/er/est
i=1
while [ "$i" -le 30 ]; do
  name=$(printf 'f%02d' "$i")
  make_file "m/$name" "$((i * 5))"
  i=$((i + 1))
done
make_file m/deep/er/est/huge 900
echo "--- top-largest default count ---"
"$BIN" -c 'koshkit du -T m' | mask_sizes > top-m.out
printf 'top-lines=%s\n' "$(wc -l < top-m.out | tr -d ' ')"
head -n 8 top-m.out
echo "..."
tail -n 3 top-m.out
for kept in deep er est huge f30 f15; do
  if grep -q " $kept\$" top-m.out; then
    echo "top-has-$kept=yes"
  else
    echo "top-has-$kept=no"
  fi
done
for dropped in f14 f01; do
  if grep -q " $dropped\$" top-m.out; then
    echo "top-has-$dropped=yes"
  else
    echo "top-has-$dropped=no"
  fi
done
echo "--- top-largest orders by name on equal sizes ---"
mkdir ties
i=1
while [ "$i" -le 25 ]; do
  make_file "ties/$(printf 'n%02d' "$i")" 1
  i=$((i + 1))
done
"$BIN" -c 'koshkit du -T ties' | mask_sizes | sed -n '2p;3p;21p'
printf 'ties-lines=%s\n' "$("$BIN" -c 'koshkit du -T ties' | wc -l | tr -d ' ')"
echo "--- top-largest operands ---"
(cd t && "$BIN" -c 'koshkit du -T .') | mask_sizes | head -n 3
"$BIN" -c 'koshkit du -T t/a t/c' | mask_sizes

mkdir -p locked-tree/open locked-tree/locked
make_file locked-tree/open/file 30
make_file locked-tree/locked/hidden 10
chmod 000 locked-tree/locked
echo "--- top-largest with an unreadable directory ---"
if ls locked-tree/locked >/dev/null 2>&1; then
  chmod 700 locked-tree/locked
  echo "locked-readable=skipped"
else
  "$BIN" -c 'koshkit du -T locked-tree' >locked.out 2>locked.err
  locked_status=$?
  chmod 700 locked-tree/locked
  mask_sizes < locked.out
  printf 'locked-status=%s\n' "$locked_status"
  printf 'locked-errors=%s\n' "$(grep -c "cannot read 'locked-tree/locked'" locked.err)"
fi

echo "--- help ---"
"$BIN" -c 'koshkit du --help' | grep -e '--tree' -e '--top-largest' -e 'Usage'

cd /
"$BIN" -c "koshkit rm -rf '$d'"
