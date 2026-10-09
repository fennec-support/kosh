unset KOSH_FLAGS KOSHCONF
# The user configuration file applies before the session runs, in interactive
# and non-interactive shells. Blanks around a value are ignored. A malformed
# line, an unknown name with its suggestion, and a value the kosh mood holds
# are located warnings that never stop the shell. KOSHCONF from the environment applies after the file and is
# removed. A command-line flag and -Q win over the file, and the kosh mood
# sources no rc file.
home=$(mktemp -d)
trap '[ -n "$home" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$home"' EXIT
export HOME="$home"
unset XDG_CONFIG_HOME
conf="$home/.config/kosh/kosh.conf"
mkdir -p "$home/.config/kosh"

cat >"$conf" <<'EOF'
# a comment and a blank line

kosh.editor.auto_close_brackets_and_quotes=on
kosh.interpreter.resolve_koshkit_applets_as_commands = true
kosh.editor.history.max_entries="128"
kosh.optimizer.warning_level=1
no equals sign
unknown.option=on
kosh.editor.hints.show_command_synopsis=maybe
kosh.interpreter.unset_variable_is_error=off
kosh.editor.history.file_path="unterminated
kosh.mood='bash' trailing
EOF
echo "== the file applies with a warning per malformed line:"
"$BIN" -c 'for name in kosh.editor.auto_close_brackets_and_quotes kosh.interpreter.resolve_koshkit_applets_as_commands kosh.editor.history.max_entries kosh.optimizer.warning_level kosh.mood; do
  printf "%s=%s\n" "$name" "$(koshconf get "$name")"
done' >"$home/out" 2>&1
status=$?
sed "s|$home|HOME|" "$home/out"
echo "rc=$status"

echo "== a byte order mark is skipped, and raw bytes are escaped in warnings:"
printf '\357\273\277kosh.editor.auto_close_brackets_and_quotes=on\r\nkosh.editor.history.max_entries=-5\r\n' >"$conf"
printf 'kosh.editor.history.file_path=a\377b\nbad\033[1mname=on\n' >>"$conf"
"$BIN" -c 'koshconf get kosh.editor.auto_close_brackets_and_quotes; koshconf get kosh.editor.history.max_entries' \
  >"$home/out" 2>&1
status=$?
sed "s|$home|HOME|" "$home/out" | od -An -c | grep -c '033'
sed "s|$home|HOME|" "$home/out"
echo "rc=$status"

printf 'kosh.mood=bash\nkosh.editor.auto_close_brackets_and_quotes=on\nkosh.interpreter.resolve_koshkit_applets_as_commands=off\nkosh.optimizer.warning_level=2\n' >"$conf"
echo "== a configured mood selects the session mood:"
"$BIN" -c 'koshconf get kosh.mood; echo "${BASH_VERSION:+bash identity}"'
echo "== a command-line flag wins over the file:"
"$BIN" --mood kosh --enable-koshkit -W -c 'koshconf get kosh.mood
koshconf get kosh.interpreter.resolve_koshkit_applets_as_commands
koshconf get kosh.optimizer.warning_level'
echo "== -Q skips the configuration files:"
"$BIN" -Q -c 'koshconf get kosh.mood; koshconf get kosh.editor.auto_close_brackets_and_quotes'
echo "== XDG_CONFIG_HOME replaces the default directory:"
XDG_CONFIG_HOME="$home/elsewhere" "$BIN" -c 'koshconf get kosh.mood'

echo "== KOSHCONF applies after the file and leaves the environment:"
KOSHCONF=AQEBBQEA "$BIN" -c 'koshconf get kosh.mood; koshconf get kosh.editor.auto_close_brackets_and_quotes
env | grep -c "^KOSHCONF="'
echo "== an invalid KOSHCONF is a warning:"
KOSHCONF='%%%' "$BIN" -c 'koshconf get kosh.mood'
echo "rc=$?"

echo "== the kosh mood sources no rc file:"
printf 'echo koshrc-ran\n' >"$home/.koshrc"
printf 'kosh.mood=kosh\n' >"$conf"
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c koshrc-ran
echo "== an interactive kosh mood warns once about a retired koshrc:"
printf 'set -L kosh\nset -L kosh\n' | "$BIN" -i 2>&1 |
  grep 'no longer read' | sed -e "s|$home|HOME|" -e "s|' and '/etc/koshrc'|'|"
echo "== -Q and a non-interactive shell do not warn:"
"$BIN" -Q -i <"$TEST_NULL_DEVICE" 2>&1 | grep -c 'no longer read'
"$BIN" -c : 2>&1 | grep -c 'no longer read'
echo "== a configured bash mood sources the bash rc:"
printf 'echo bashrc-ran\n' >"$home/.bashrc"
printf 'kosh.mood=bash\n' >"$conf"
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
echo "== an inherited KOSH_HISTORY_SIZE is the kosh.editor.history.max_entries option:"
printf 'kosh.mood=kosh\n' >"$conf"
KOSH_HISTORY_SIZE=77 "$BIN" -c 'koshconf get kosh.editor.history.max_entries; (koshconf get kosh.editor.history.max_entries) & wait "$!"'
echo "== an inherited history variable wins over the file, and neither is exported by default:"
printf 'kosh.editor.history.file_path=/from-file\nkosh.editor.history.max_entries=88\n' >"$conf"
(
  unset KOSH_HISTORY_FILE KOSH_HISTORY_SIZE
  "$BIN" -c 'echo "$KOSH_HISTORY_FILE $KOSH_HISTORY_SIZE"
env | grep -c "^KOSH_HISTORY_" || :'
  KOSH_HISTORY_FILE=/inherited KOSH_HISTORY_SIZE=5 "$BIN" -c '
echo "$(koshconf get kosh.editor.history.file_path) $(koshconf get kosh.editor.history.max_entries)"
env | grep "^KOSH_HISTORY_"'
)
echo "== an inherited KOSH is replaced and no longer exported:"
KOSH=/elsewhere KOSH_VERSION=0 "$BIN" -c '[ "$KOSH" != /elsewhere ] && echo replaced
env | grep -c -e "^KOSH=" -e "^KOSH_VERSION=" || :
sh -c "test -z \"\$KOSH\" && echo unset-in-a-child-shell"'
echo "== privileged mode ignores and removes KOSHCONF and still reads the file:"
printf 'kosh.editor.auto_close_brackets_and_quotes=on\n' >"$conf"
KOSHCONF=BQEA "$BIN" -p -c 'koshconf get kosh.editor.auto_close_brackets_and_quotes
env | grep -c "^KOSHCONF="'
KOSHCONF=BQEA "$BIN" -c 'koshconf get kosh.editor.auto_close_brackets_and_quotes'
echo "== semantic options from the file apply to scripts, after the mood:"
printf 'kosh.interpreter.exit_on_command_failure=on\nkosh.interpreter.glob_includes_dotfiles=on\nkosh.mood=bash\n' >"$conf"
printf 'false\necho unreached\n' >"$home/script.sh"
"$BIN" "$home/script.sh"
echo "rc=$?"
"$BIN" -c 'shopt dotglob; set -M'
echo "== a kosh mood value that conflicts with a fixed option is skipped:"
printf 'kosh.interpreter.unset_variable_is_error=off\nkosh.interpreter.glob_no_match_expands_to_nothing=off\n' >"$conf"
"$BIN" -c 'koshconf get kosh.interpreter.unset_variable_is_error' 2>&1 |
  sed "s|$home|HOME|"
echo "== an unknown name in the file suggests a close one:"
printf 'kosh.editor.history.max_entry=3\n' >"$conf"
"$BIN" -c 'koshconf get kosh.editor.history.max_entries' 2>&1 | sed "s|$home|HOME|"
echo "== a command-line flag wins over a semantic option from the file:"
printf 'kosh.mood=bash\nkosh.interpreter.unset_variable_is_error=off\n' >"$conf"
"$BIN" -u -c 'koshconf get kosh.interpreter.unset_variable_is_error'

echo "== the editor and trace options apply from the file:"
printf 'kosh.completion.on_tab=off\nkosh.editor.highlight_syntax_and_show_ghost_text=off\nkosh.optimizer.show_source_traces=off\n' >"$conf"
"$BIN" -c 'koshconf get kosh.completion.on_tab
koshconf get kosh.editor.highlight_syntax_and_show_ghost_text
koshconf get kosh.optimizer.show_source_traces'
echo "== KOSHCONF carries them:"
"$BIN" -c 'export KOSHCONF; XDG_CONFIG_HOME="$1" "$KOSH" -c "koshconf get kosh.completion.on_tab; koshconf get kosh.optimizer.show_source_traces"' \
  sh "$home/elsewhere"
echo "== a file without traces drops the trace rows:"
printf 'f() { missing_command_xyz; }\nf\n' >"$home/lib.sh"
"$BIN" -c '. "$1"' sh "$home/lib.sh" 2>&1 | grep -c 'trace:'
"$BIN" -Q -c '. "$1"' sh "$home/lib.sh" 2>&1 | grep -c 'trace:'
echo "== the command-line flags win over the file:"
printf 'kosh.completion.on_tab=on\nkosh.editor.highlight_syntax_and_show_ghost_text=on\nkosh.optimizer.show_source_traces=on\n' >"$conf"
"$BIN" -T --no-syntax-highlighting --no-traces -c 'koshconf get kosh.completion.on_tab
koshconf get kosh.editor.highlight_syntax_and_show_ghost_text
koshconf get kosh.optimizer.show_source_traces'
"$BIN" --dumb -c 'koshconf get kosh.completion.on_tab'

echo "== kosh.init_moods selects the startup files of an interactive shell:"
printf 'kosh.init_moods=bash\n' >"$conf"
"$BIN" -c 'koshconf get kosh.init_moods; koshconf get kosh.mood'
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
echo "== a script ignores it and keeps the kosh identity:"
"$BIN" -c 'echo "bash version: ${BASH_VERSION:-unset}"'
echo "== -L wins over the file:"
"$BIN" -L kosh -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
"$BIN" -L sh,bash -c 'koshconf get kosh.init_moods'
echo "== an unknown mood in the list is a warning:"
printf 'kosh.init_moods=bash,zsh\n' >"$conf"
"$BIN" -c 'koshconf get kosh.init_moods' 2>&1 | sed "s|$home|HOME|"
echo "== koshconf set validates the list:"
: >"$conf"
"$BIN" -c 'koshconf set kosh.init_moods sh,bash-posix; koshconf get kosh.init_moods
koshconf set kosh.init_moods ksh' 2>&1 | grep -v '^ '
