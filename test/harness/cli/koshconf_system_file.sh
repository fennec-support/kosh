unset KOSH_FLAGS KOSHCONF KOSH_DEBUG_SYSTEM_KOSHCONF
# The system kosh.conf applies only when the file and its directory belong to
# root and neither is writable by a group or by others. A user namespace maps
# the test user to root and mounts a private /etc, so each case can set the
# owner and mode the check reads.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
printf 'editor.auto_close_brackets_and_quotes=on\n' >"$work/kosh.conf"

if ! unshare -rm true 2>/dev/null; then
  echo "user namespaces are unavailable"
  exit 0
fi

do_case() {
  file_mode=$1
  directory_mode=$2
  owner=$3
  echo "== file $file_mode, /etc $directory_mode, owner $owner:"
  unshare -rm /bin/sh -c '
    mount -t tmpfs -o mode=755 tmpfs /etc || exit 1
    cp "$1/kosh.conf" /etc/kosh.conf
    chmod "$2" /etc/kosh.conf
    chmod "$3" /etc
    if [ "$4" = nobody ]; then
      mount --bind /usr/bin/env /etc/kosh.conf
    fi
    "$5" -c "koshconf get editor.auto_close_brackets_and_quotes"
  ' sh "$work" "$file_mode" "$directory_mode" "$owner" "$BIN" 2>&1
}

do_case 644 755 root
do_case 664 755 root
do_case 646 755 root
do_case 644 775 root
do_case 644 755 nobody
