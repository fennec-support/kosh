unset KOSH_FLAGS
# touch sets the modification time of an existing file to the current time rather
# than passing it over. A hermetic temp directory keeps it stable, and it is left
# in place so the test never runs rm.
# GNU stat prints the modification epoch with -c, BSD stat with -f, so the helper
# tries the GNU form first and falls back to the BSD one on macOS.
stat_mtime() { stat -c '%Y' "$1" 2>/dev/null || stat -f '%m' "$1"; }
stat_atime() { stat -c '%X' "$1" 2>/dev/null || stat -f '%a' "$1"; }
dir=$(mktemp -d)
: > "$dir/f"
before=$(stat_mtime "$dir/f")
sleep 1.1
"$BIN" -c "koshkit touch '$dir/f'" </dev/null
after=$(stat_mtime "$dir/f")
echo "== touch advances the modification time of an existing file:"
if [ "$after" -gt "$before" ]; then
  echo advanced
else
  echo unchanged
fi
echo "== touch -c still does not create a missing file:"
"$BIN" -c "koshkit touch -c '$dir/ghost'" </dev/null
"$BIN" -c "if [ -e '$dir/ghost' ]; then echo ghost-exists; else echo ghost-missing; fi" </dev/null
echo "== touch -r copies both timestamps:"
: > "$dir/reference"
: > "$dir/copied"
touch -t 200102030405.06 "$dir/reference"
"$BIN" -c "koshkit touch -r '$dir/reference' '$dir/copied'" </dev/null
if [ "$(stat_atime "$dir/copied")" -eq "$(stat_atime "$dir/reference")" ] &&
  [ "$(stat_mtime "$dir/copied")" -eq "$(stat_mtime "$dir/reference")" ]; then
  echo copied
else
  echo different
fi
echo "== touch -a updates the access time and keeps the modification time:"
before_atime=$(stat_atime "$dir/copied")
before_mtime=$(stat_mtime "$dir/copied")
"$BIN" -c "koshkit touch -a '$dir/copied'" </dev/null
if [ "$(stat_atime "$dir/copied")" -gt "$before_atime" ] &&
  [ "$(stat_mtime "$dir/copied")" -eq "$before_mtime" ]; then
  echo updated
else
  echo wrong
fi
echo "== touch -m updates the modification time and keeps the access time:"
before_atime=$(stat_atime "$dir/copied")
before_mtime=$(stat_mtime "$dir/copied")
"$BIN" -c "koshkit touch -m '$dir/copied'" </dev/null
if [ "$(stat_mtime "$dir/copied")" -gt "$before_mtime" ] &&
  [ "$(stat_atime "$dir/copied")" -eq "$before_atime" ]; then
  echo updated
else
  echo wrong
fi
echo "== touch -t sets the requested timestamp:"
"$BIN" -c "koshkit touch -t 200102030405.06 '$dir/copied'" </dev/null
[ "$(stat_mtime "$dir/copied")" -eq "$(stat_mtime "$dir/reference")" ] && echo set || echo different
echo "== touch -t accepts an eight-digit timestamp:"
touch -t 01020304 "$dir/reference"
"$BIN" -c "koshkit touch -t 01020304 '$dir/copied'" </dev/null
[ "$(stat_mtime "$dir/copied")" -eq "$(stat_mtime "$dir/reference")" ] && echo set || echo different
echo "== touch -t accepts a ten-digit timestamp:"
touch -t 0102030405 "$dir/reference"
"$BIN" -c "koshkit touch -t 0102030405 '$dir/copied'" </dev/null
[ "$(stat_mtime "$dir/copied")" -eq "$(stat_mtime "$dir/reference")" ] && echo set || echo different
