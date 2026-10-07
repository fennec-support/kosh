unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
# KOSH names the file that started the shell. A name with a slash is made
# absolute, and a bare name is found on the PATH instead of being joined to the
# working directory.
d=$(mktemp -d)
real_d=$(CDPATH= cd -- "$d" && pwd -P)
mkdir "$real_d/bin" "$real_d/work"
ln -s "$BIN" "$real_d/bin/kosh"
show='echo "KOSH=$KOSH"; echo "SHELL=$SHELL"; command -v kosh'
echo "== a bare name is resolved through the PATH:"
(cd "$real_d/work" && env -i HOME="$HOME" PATH="$real_d/bin:/usr/bin:/bin" kosh -c "$show") | sed "s#$real_d#D#g"
echo "== a bare name found in a later PATH entry:"
(cd "$real_d/work" && env -i HOME="$HOME" PATH="/usr/bin:$real_d/bin:/bin" kosh -c "$show") | sed "s#$real_d#D#g"
echo "== a name with a slash is made absolute:"
(cd "$real_d/bin" && env -i HOME="$HOME" PATH="/usr/bin:/bin" ./kosh -c "$show") | sed "s#$real_d#D#g"
echo "== the inherited PATH is kept:"
env -i HOME="$HOME" PATH="$real_d/bin:/usr/bin:/bin" "$real_d/bin/kosh" -c 'echo "PATH=$PATH"' | sed "s#$real_d#D#g"
echo "== a clean start resets the PATH and KOSH stays absolute:"
(cd "$real_d/work" && env -i HOME="$HOME" PATH="$real_d/bin:/usr/bin:/bin" kosh --no-init-files -c 'case $KOSH in /*) echo absolute;; *) echo "relative:$KOSH";; esac; echo "PATH=$PATH"')
echo "== KOSH is set in every mood, after a mood switch, and under a bash name:"
ln -s "$BIN" "$real_d/bin/bash"
for mood in kosh bash sh bash-posix; do
  "$BIN" --mood "$mood" -c 'printf "%s " "${KOSH:+set}"'
done
"$BIN" -c 'set -M sh; printf "%s " "${KOSH:+set}"'
"$real_d/bin/bash" -c 'printf "%s\n" "${KOSH:+set}"'
echo "== KOSH replaces an inherited value and is not exported:"
KOSH=inherited "$BIN" --mood bash -c 'case $KOSH in inherited) echo kept;; *) echo replaced;; esac'
"$BIN" --mood bash -c 'env | grep -c "^KOSH="'
[ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$d"
