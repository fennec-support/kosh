# man completes the names of the indexed section 1 pages for a man binary of
# that name, for a man that links to mandoc, and for the bundled koshkit man.
# A word that names no page falls through to the other stages, so a file still
# completes. The page names share a prefix no host page has.
dir=$(mktemp -d)
mkdir -p "$dir/man/man1" "$dir/plain" "$dir/linked" "$dir/work"
for page in zqpage-alpha zqpage-beta; do
  : >"$dir/man/man1/$page.1"
done
printf '#!/bin/sh\n' >"$dir/plain/man"
printf '#!/bin/sh\n' >"$dir/linked/mandoc"
chmod +x "$dir/plain/man" "$dir/linked/mandoc"
ln -s mandoc "$dir/linked/man"
: >"$dir/work/zqfile.txt"
cd "$dir/work" || exit 1
echo "== a man binary called man:"
PATH="$dir/plain:$PATH" MANPATH="$dir/man" "$BIN" \
  --debug-complete-at 'man zqp' </dev/null
echo "== a man that links to mandoc:"
PATH="$dir/linked:$PATH" MANPATH="$dir/man" "$BIN" \
  --debug-complete-at 'man zqp' </dev/null
echo "== koshkit man:"
PATH="$dir/plain:$PATH" MANPATH="$dir/man" "$BIN" \
  --debug-complete-at 'koshkit man zqpage-b' </dev/null
echo "== a word that names no page completes a file:"
PATH="$dir/plain:$PATH" MANPATH="$dir/man" "$BIN" \
  --debug-complete-at 'man zqf' </dev/null
cd / || exit 1
rm -rf "$dir"
