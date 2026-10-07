unset KOSH_FLAGS KOSHCONF
# create and --persist write through a symlinked kosh.conf to its target, keep
# the mode of an existing file, give a new file 0644 less the umask, leave no
# temporary file behind, and serialize writers so that two concurrent persists
# of different options both survive.
config=$(mktemp -d)
trap '[ -n "$config" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$config"' EXIT
export XDG_CONFIG_HOME="$config/user"
dotfiles=$config/dotfiles
mkdir -p "$XDG_CONFIG_HOME/kosh" "$dotfiles"
conf=$XDG_CONFIG_HOME/kosh/kosh.conf

do_mode() {
  "$BIN_DIR/invoke-koshkit" stat -c %a "$1"
}

echo "== a symlinked file stays a link and its target is updated:"
printf 'mood=kosh\n' >"$dotfiles/kosh.conf"
ln -s ../../dotfiles/kosh.conf "$conf"
"$BIN" -c 'koshconf set editor.auto_pair on --persist'
echo "rc=$?"
[ -L "$conf" ] && echo link-kept
cat "$dotfiles/kosh.conf"
echo "== create --force also writes through the link:"
"$BIN" -c 'koshconf create --force sh'
echo "rc=$?"
[ -L "$conf" ] && echo link-kept
grep '^mood=' "$dotfiles/kosh.conf"
echo "== a dangling link gets its target created:"
"$BIN_DIR/invoke-koshkit" rm -f -- "$dotfiles/kosh.conf"
"$BIN" -c 'koshconf set editor.hints off --persist'
echo "rc=$?"
[ -L "$conf" ] && echo link-kept
cat "$dotfiles/kosh.conf"
echo "== no temporary file is left in either directory:"
"$BIN_DIR/invoke-koshkit" ls -A "$XDG_CONFIG_HOME/kosh"
"$BIN_DIR/invoke-koshkit" ls -A "$dotfiles"

echo "== an existing mode is kept:"
chmod 600 "$dotfiles/kosh.conf"
"$BIN" -c 'koshconf set editor.hints on --persist'
do_mode "$dotfiles/kosh.conf"
chmod 640 "$dotfiles/kosh.conf"
"$BIN" -c 'koshconf create --force kosh'
do_mode "$dotfiles/kosh.conf"
echo "== a new file is 0644 less the umask:"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
(umask 022 && "$BIN" -c 'koshconf set editor.hints on --persist')
do_mode "$conf"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
(umask 077 && "$BIN" -c 'koshconf create kosh')
do_mode "$conf"

echo "== concurrent persists of different options both survive:"
lost_count=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  printf 'mood=kosh\n' >"$conf"
  "$BIN" -c 'koshconf set editor.auto_pair on --persist' &
  "$BIN" -c 'koshconf set completion.space_after off --persist' &
  wait
  if ! grep -q '^editor.auto_pair=on$' "$conf" ||
    ! grep -q '^completion.space_after=off$' "$conf"
  then
    lost_count=$((lost_count + 1))
    echo "attempt $attempt lost a setting"
  fi
done
echo "lost=$lost_count"
