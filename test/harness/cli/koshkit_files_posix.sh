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
