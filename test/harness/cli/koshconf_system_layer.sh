unset KOSH_FLAGS KOSHCONF KOSH_DEBUG_SYSTEM_KOSHCONF
# koshconf create leaves every setting but the mood commented out, so the
# system kosh.conf still applies, and save --persist writes the settings that
# differ from the system file or, without a system line, from the defaults. A
# user namespace maps root and a second user, so /etc/kosh/kosh.conf belongs to
# root while the shell runs as the user.
unshare --map-root-user --map-auto -m /bin/sh -c '
  mount -t tmpfs -o mode=755 tmpfs /etc || exit 1
  mkdir /etc/kosh
  printf "%s\n" editor.auto_close_brackets_and_quotes=on completion.on_tab=off \
    >/etc/kosh/kosh.conf
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

# The system rc.kosh beside kosh.conf passes the same checks and runs before
# the user rc.kosh. The shell runs as the second user, since the superuser
# skips the user rc.kosh.
do_rc_case() {
  rc_mode=$1
  echo "== system rc.kosh $rc_mode:"
  unshare --map-root-user --map-auto -m /bin/sh -c '
    mount -t tmpfs -o mode=755 tmpfs /etc || exit 1
    mkdir /etc/kosh
    echo "echo system rc" >/etc/kosh/rc.kosh
    chmod "$1" /etc/kosh/rc.kosh
    mount -t tmpfs -o mode=777 tmpfs /mnt || exit 1
    exec setpriv --reuid=1000 --regid=1000 --clear-groups /bin/sh -c "
      export HOME=/mnt XDG_CONFIG_HOME=/mnt/user
      mkdir -p /mnt/user/kosh/scripts
      echo \"echo user rc\" >/mnt/user/kosh/scripts/rc.kosh
      \"\$0\" -l -c \"echo command\"
    " "$2"
  ' sh "$rc_mode" "$BIN" 2>&1
}

do_rc_case 644
do_rc_case 666
