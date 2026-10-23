unset KOSH_FLAGS KOSHCONF KOSH_DEBUG_SYSTEM_KOSHCONF
# koshconf create leaves every setting but the mood commented out, so the
# system kosh.conf still applies, and save --persist writes the settings that
# differ from the system file or, without a system line, from the defaults. A
# user namespace maps root and a second user, so /etc/kosh.conf belongs to
# root while the shell runs as the user.
unshare --map-root-user --map-auto -m /bin/sh -c '
  mount -t tmpfs -o mode=755 tmpfs /etc || exit 1
  printf "%s\n" editor.auto_close_brackets_and_quotes=on completion.on_tab=off \
    >/etc/kosh.conf
  mount -t tmpfs -o mode=777 tmpfs /mnt || exit 1
  exec setpriv --reuid=1000 --regid=1000 --clear-groups /bin/sh -c "
    export HOME=/mnt XDG_CONFIG_HOME=/mnt/user
    conf=/mnt/user/kosh/kosh.conf
    echo \"== create keeps only the mood active:\"
    \"\$0\" -c \"koshconf create kosh\" 2>/dev/null
    grep -v \"^#\" \"\$conf\" | grep .
    \"\$0\" -c \"koshconf get completion.on_tab\"
    echo \"== save --persist writes the settings that differ from the system file:\"
    \"\$0\" -c \"koshconf set completion.on_tab on
      koshconf set editor.auto_close_brackets_and_quotes on
      koshconf set editor.hints.show_command_synopsis off
      koshconf save --persist\" 2>/dev/null
    grep -v \"^#\" \"\$conf\" | grep .
    \"\$0\" -c \"koshconf get completion.on_tab
      koshconf get editor.auto_close_brackets_and_quotes\"
  " "$1"
' sh "$BIN" 2>&1
