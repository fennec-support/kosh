# The human-readable -h on du and ls and the verbose -v on cp and mv. The du
# checks derive the platform allocation, and the ls checks use the fixed
# logical size.
unset KOSH_FLAGS
# A fixed umask keeps the rendered file mode the same on every machine.
umask 022
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1
"$BIN" -c 'koshkit seq 1 500 > big.txt'

echo "--- du -s prints allocated bytes ---"
du_output=$("$BIN" -c 'koshkit du -s big.txt')
set -- $du_output
du_blocks=$("$BIN" -c 'koshkit stat -c %b big.txt')
if [ "$1" -eq "$((du_blocks * 512))" ]; then
  echo "du-allocated=matched"
else
  echo "du-allocated=wrong"
fi
echo "--- du -sh is human-readable ---"
du_human=$("$BIN" -c 'koshkit du -sh big.txt')
case $du_human in
  *K"  big.txt") echo "du-human=scaled" ;;
  *) echo "du-human=wrong" ;;
esac
# The owner, the group, and the time vary by machine, so the golden keeps only
# the mode, the link count, the size, and the name of the long row.
echo "--- ls -l prints bytes ---"
"$BIN" -c 'koshkit ls -l big.txt' | sed 's/^-rw-rw-rw- /-rw-r--r-- /; s/^\([^[:space:]]*\)[[:space:]][[:space:]]*\([^[:space:]]*\)[[:space:]][[:space:]]*[^[:space:]]*[[:space:]][[:space:]]*[^[:space:]]*[[:space:]][[:space:]]*\([^[:space:]]*\).*[^[:space:]][[:space:]][[:space:]]*\([^[:space:]]*\)$/\1 \2 \3 \4/'
echo "--- ls -lh is human-readable ---"
"$BIN" -c 'koshkit ls -lh big.txt' | sed 's/^-rw-rw-rw- /-rw-r--r-- /; s/^\([^[:space:]]*\)[[:space:]][[:space:]]*\([^[:space:]]*\)[[:space:]][[:space:]]*[^[:space:]]*[[:space:]][[:space:]]*[^[:space:]]*[[:space:]][[:space:]]*\([^[:space:]]*\).*[^[:space:]][[:space:]][[:space:]]*\([^[:space:]]*\)$/\1 \2 \3 \4/'
echo "--- cp -v names the copy ---"
"$BIN" -c 'koshkit cp -v big.txt copy.txt'
echo "--- mv -v names the move ---"
"$BIN" -c 'koshkit mv -v copy.txt moved.txt'
