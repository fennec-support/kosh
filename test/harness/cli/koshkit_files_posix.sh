# The koshkit file utilities report POSIX lookup errors and resolve users
# through the user database. The commands run inside a fresh temporary
# directory so the output has no absolute path.
unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
trap 'cd / && [ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$d"' EXIT
cd "$d" || exit 1

echo "--- rm reports the error of the failed lookup ---"
: > rm-plain.txt
"$BIN" -c 'koshkit rm rm-plain.txt/x 2>&1; printf "status=%s\n" "$?"; koshkit rm rm-missing.txt 2>&1; printf "status=%s\n" "$?"'
echo "--- id looks up the given user ---"
"$BIN" -c 'koshkit id -g root; koshkit id -G root | { read -r first rest; printf "%s\n" "$first"; }; koshkit id -un 0'
echo "--- chown owner colon sets the login group ---"
: > chown-login.txt
"$BIN" -c 'koshkit chown "$(koshkit id -un):" chown-login.txt; printf "status=%s\n" "$?"'
echo "--- cp -l and -s link instead of copying ---"
mkdir cp-link-tree cp-link-sub
printf one > cp-link-tree/file
printf old > cp-link-old
printf old > cp-link-sub/forced
"$BIN" -c 'koshkit cp -lv cp-link-tree/file cp-link-hard; koshkit cp -Rl cp-link-tree cp-link-tree-copy; koshkit cp -l cp-link-tree/file cp-link-old 2>/dev/null; printf "existing=%s\n" "$?"; koshkit cp -ln cp-link-tree/file cp-link-old; koshkit cp -lf cp-link-tree/file cp-link-sub/forced; koshkit cp -sv cp-link-old cp-link-soft; koshkit cp -s cp-link-old cp-link-sub/soft 2>/dev/null; printf "relative-outside=%s\n" "$?"; koshkit cp -ls cp-link-old cp-link-both 2>/dev/null; printf "both=%s\n" "$?"'
for cp_link_path in cp-link-hard cp-link-tree-copy/file cp-link-sub/forced; do
  if [ "$cp_link_path" -ef cp-link-tree/file ] && [ ! -L "$cp_link_path" ]; then
    echo "$cp_link_path=hard"
  else
    echo "$cp_link_path=not-hard"
  fi
done
printf 'kept=%s soft=%s sub-soft=%s both=%s\n' "$(cat cp-link-old)" "$(readlink cp-link-soft)" "$([ -e cp-link-sub/soft ] && echo present || echo missing)" "$([ -e cp-link-both ] && echo present || echo missing)"
