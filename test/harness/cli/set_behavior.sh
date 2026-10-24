unset KOSH_FLAGS
# An unknown -M value is rejected, and set -o with no name lists the Bash
# option states.
echo "== an unknown mood is rejected:"
"$BIN" -c 'set -M badmood'; echo "rc=$?"
echo "== set -o lists the option states:"
"$BIN" -c 'set -o'

unset KOSH_FLAGS
# set -b backs the notify option, so the state round-trips through set +o and
# the letter form.
"$BIN" -c 'set -o | grep notify'
"$BIN" -c 'set -b; set -o | grep notify'
"$BIN" -c 'set -o notify; set +b; set -o | grep notify'
echo "rc=$?"

unset KOSH_FLAGS
# set -o posix mirrors set -M sh, entering the POSIX mood. set +o posix
# steps down to bash only when already in POSIX, and is a no-op otherwise since
# the prior mood is not recoverable.
echo "== set -o posix enters posix mood:"
"$BIN" -c 'set -o posix; set -o | grep posix'
echo "== set +o posix from bash mood is a no-op:"
"$BIN" -M bash -c 'set +o posix; set -M; set -o | grep posix'
echo "== set +o posix from posix mood steps to bash:"
"$BIN" -M bash -c 'set -o posix; set -M; set +o posix; set -M; set -o | grep posix'
echo "== set +o posix leaves builtin enable usable:"
"$BIN" -M bash -c 'set +o posix; builtin enable compgen unset; echo ok'
echo "rc-done"

unset KOSH_FLAGS
# A login shell marks its mood initialized, and set -L lists the same moods
# inside a background subshell as in the shell that started it.
home=$(mktemp -d)
trap '[ -n "$home" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$home"' EXIT
echo "== a background subshell lists the initialized moods:"
HOME="$home" "$BIN" -l -c 'set -L; ( set -L ) & wait "$!"' \
  2>/dev/null
echo "rc=$?"
