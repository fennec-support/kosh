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
"$BIN" -c 'koshconf set editor.auto_close_brackets_and_quotes on --persist'
echo "rc=$?"
[ -L "$conf" ] && echo link-kept
cat "$dotfiles/kosh.conf"
echo "== create --force also writes through the link:"
"$BIN" -c 'koshconf create --force sh'
echo "rc=$?"
[ -L "$conf" ] && echo link-kept
grep '^mood=' "$dotfiles/kosh.conf"
echo "== an override keeps the previous file in .bak:"
[ -f "$dotfiles/kosh.conf.bak" ] && echo backup-created
grep '^mood=' "$dotfiles/kosh.conf.bak"
echo "== each set --persist keeps the file it replaces in .bak:"
"$BIN" -c 'koshconf set editor.history.max_entries 5 --persist'
grep -c '^mood=sh$' "$dotfiles/kosh.conf.bak"
"$BIN" -c 'koshconf set editor.history.max_entries 6 --persist'
grep -c '^editor.history.max_entries=5$' "$dotfiles/kosh.conf.bak"
echo "== a dangling link gets its target created:"
"$BIN_DIR/invoke-koshkit" rm -f -- "$dotfiles/kosh.conf"
"$BIN" -c 'koshconf set editor.hints.show_command_synopsis off --persist'
echo "rc=$?"
[ -L "$conf" ] && echo link-kept
cat "$dotfiles/kosh.conf"
echo "== no temporary file is left in either directory:"
"$BIN_DIR/invoke-koshkit" ls -A "$XDG_CONFIG_HOME/kosh"
"$BIN_DIR/invoke-koshkit" ls -A "$dotfiles"

echo "== an existing mode is kept:"
chmod 600 "$dotfiles/kosh.conf"
"$BIN" -c 'koshconf set editor.hints.show_command_synopsis on --persist'
do_mode "$dotfiles/kosh.conf"
chmod 640 "$dotfiles/kosh.conf"
"$BIN" -c 'koshconf create --force kosh'
do_mode "$dotfiles/kosh.conf"
echo "== a new file is 0644 less the umask:"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
(umask 022 && "$BIN" -c 'koshconf set editor.hints.show_command_synopsis on --persist')
do_mode "$conf"
"$BIN_DIR/invoke-koshkit" rm -rf -- "$XDG_CONFIG_HOME"
(umask 077 && "$BIN" -c 'koshconf create kosh')
do_mode "$conf"

echo "== concurrent persists of different options both survive:"
lost_count=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  printf 'mood=kosh\n' >"$conf"
  "$BIN" -c 'koshconf set editor.auto_close_brackets_and_quotes on --persist' &
  "$BIN" -c 'koshconf set completion.add_space_after_completed_word off --persist' &
  wait
  if ! grep -q '^editor.auto_close_brackets_and_quotes=on$' "$conf" ||
    ! grep -q '^completion.add_space_after_completed_word=off$' "$conf"
  then
    lost_count=$((lost_count + 1))
    echo "attempt $attempt lost a setting"
  fi
done
echo "lost=$lost_count"

# Each writer has its own HOME, and all of them share one kosh.conf.
echo "== eight concurrent persists leave a well-formed file with every setting:"
printf 'mood=kosh\n' >"$conf"
writer_index=0
for setting in \
  'editor.auto_close_brackets_and_quotes on' \
  'completion.add_space_after_completed_word on' \
  'editor.hints.show_command_synopsis off' \
  'editor.hints.show_live_diagnostics off' \
  'editor.history.arrow_keys_search_by_typed_prefix off' \
  'editor.request_extended_key_reports off' \
  'editor.history.max_entries 77' \
  'editor.wrap_width 70'
do
  writer_index=$((writer_index + 1))
  mkdir -p "$config/home$writer_index"
  HOME=$config/home$writer_index \
    "$BIN" -c "koshconf set $setting --persist" &
done
wait
sort "$conf"
"$BIN" -c 'koshconf get editor.wrap_width' 2>&1
"$BIN_DIR/invoke-koshkit" ls -A "$XDG_CONFIG_HOME/kosh"

# The POSIX lock is a lock on the directory, and the Windows lock is a file
# opened without sharing, so the system releases either when its holder dies.
echo "== a leftover lock file does not block a save:"
: >"$XDG_CONFIG_HOME/kosh/.kosh-flock.lock"
"$BIN" -c 'koshconf set editor.wrap_width 71 --persist'
echo "rc=$?"
grep '^editor.wrap_width=' "$conf"
"$BIN_DIR/invoke-koshkit" rm -f -- "$XDG_CONFIG_HOME/kosh/.kosh-flock.lock"
echo "== a lock holder that is killed does not block a save:"
"$BIN_DIR/invoke-koshkit" flock "$XDG_CONFIG_HOME/kosh" \
  "$BIN" -c 'kill -s KILL $$'
"$BIN_DIR/invoke-koshkit" timeout 60 \
  "$BIN" -c 'koshconf set editor.wrap_width 72 --persist'
echo "rc=$?"
grep '^editor.wrap_width=' "$conf"
