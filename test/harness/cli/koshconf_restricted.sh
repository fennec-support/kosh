unset KOSH_FLAGS KOSHCONF
# A restricted shell refuses every koshconf form that changes a setting or
# writes a file, keeps get and list, and ignores an inherited KOSHCONF blob.
config=$(mktemp -d)
trap '[ -n "$config" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$config"' EXIT
export XDG_CONFIG_HOME="$config"

for form in 'koshconf create kosh' 'koshconf create --force bash' \
  'koshconf set editor.auto_close_brackets_and_quotes on --persist' \
  'koshconf set editor.auto_close_brackets_and_quotes on' 'koshconf set mood bash' \
  'koshconf load BQEB'
do
  echo "== kosh -r: $form"
  "$BIN" -r -c "$form; koshconf get editor.auto_close_brackets_and_quotes"
  echo "rc=$?"
done
echo "== set -r refuses a later change:"
"$BIN" -c 'set -r; koshconf set editor.auto_close_brackets_and_quotes on'
echo "rc=$?"
echo "== set refuses the mood and mimicry but keeps -o posix:"
for change in 'set -M kosh' 'set -I' 'set +I' 'set -uI'; do
  "$BIN" -r --mood bash -c "$change; echo \"$change status=\$?\"" 2>&1 |
    grep -v '^ '
done
"$BIN" -r --mood bash -c 'set -M; set -o posix; echo "posix status=$?"; set -M
set +o posix; set -M; koshconf get compat.mimic_shell_named_by_shebang'
echo "== get and list stay available:"
"$BIN" -r -c 'koshconf get mood; koshconf list | grep -c .' | sed 's/^[0-9][0-9]*$/COUNT/'
echo "== no configuration file was written:"
[ -e "$config/kosh" ] && echo written || echo absent
echo "== an inherited KOSHCONF is ignored and removed:"
KOSHCONF=BQEB "$BIN" -r -c 'koshconf get editor.auto_close_brackets_and_quotes; env | grep -c "^KOSHCONF="'
echo "rc=$?"
echo "== -Q skips and removes KOSHCONF:"
KOSHCONF=BQEB "$BIN" -Q -c 'koshconf get editor.auto_close_brackets_and_quotes; env | grep -c "^KOSHCONF="'

echo "== a restricted shell keeps only harmless settings from the files:"
home=$config/home
mkdir -p "$home" "$config/kosh"
printf 'echo bashrc-ran\n' >"$home/.bashrc"
printf 'mood=bash\nkoshkit.run_utilities_as_plain_commands=on\nhistory.file_path=/elsewhere\neditor.auto_close_brackets_and_quotes=on\n' \
  >"$config/kosh/kosh.conf"
printf 'legacy.glob_includes_dotfiles=on\nstartup.init_moods=bash\n' >>"$config/kosh/kosh.conf"
HOME="$home" KOSH_HISTORY_FILE=/history "$BIN" -r -c 'koshconf get mood
koshconf get koshkit.run_utilities_as_plain_commands
koshconf get legacy.glob_includes_dotfiles
koshconf get history.file_path
koshconf get editor.auto_close_brackets_and_quotes
koshconf get startup.init_moods'
HOME="$home" "$BIN" -r -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
HOME="$home" "$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
"$BIN_DIR/invoke-koshkit" rm -rf -- "$config/kosh"

echo "== a subshell of a restricted shell keeps the restrictions:"
KOSHCONF=BQEB "$BIN" -r -c '( cd / ) & wait "$!"; echo "cd rc=$?"
# shellcheck disable=SC2123
( PATH=x ) & wait "$!"; echo "PATH rc=$?"
( koshconf set editor.auto_close_brackets_and_quotes on ) & wait "$!"; echo "koshconf rc=$?"' 2>&1 |
  grep -v '^ '
