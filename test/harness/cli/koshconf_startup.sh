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

editor.auto_close_brackets_and_quotes=on
interpreter.resolve_koshkit_applets_as_commands = true
editor.history.max_entries="128"
optimizer.warning_level=1
no equals sign
unknown.option=on
editor.hints.show_command_synopsis=maybe
interpreter.unset_variable_is_error=off
editor.history.file_path="unterminated
mood='bash' trailing
EOF
echo "== the file applies with a warning per malformed line:"
"$BIN" -c 'for name in editor.auto_close_brackets_and_quotes interpreter.resolve_koshkit_applets_as_commands editor.history.max_entries optimizer.warning_level mood; do
  printf "%s=%s\n" "$name" "$(koshconf get "$name")"
done' >"$home/out" 2>&1
status=$?
sed "s|$home|HOME|" "$home/out"
echo "rc=$status"

echo "== a byte order mark is skipped, and raw bytes are escaped in warnings:"
printf '\357\273\277editor.auto_close_brackets_and_quotes=on\r\neditor.history.max_entries=-5\r\n' >"$conf"
printf 'editor.history.file_path=a\377b\nbad\033[1mname=on\n' >>"$conf"
"$BIN" -c 'koshconf get editor.auto_close_brackets_and_quotes; koshconf get editor.history.max_entries' \
  >"$home/out" 2>&1
status=$?
sed "s|$home|HOME|" "$home/out" | od -An -c | grep -c '033'
sed "s|$home|HOME|" "$home/out"
echo "rc=$status"

printf 'mood=bash\neditor.auto_close_brackets_and_quotes=on\ninterpreter.resolve_koshkit_applets_as_commands=off\noptimizer.warning_level=2\n' >"$conf"
echo "== a configured mood selects the session mood:"
"$BIN" -c 'koshconf get mood; echo "${BASH_VERSION:+bash identity}"'
echo "== a command-line flag wins over the file:"
"$BIN" --mood kosh --enable-koshkit -W -c 'koshconf get mood
koshconf get interpreter.resolve_koshkit_applets_as_commands
koshconf get optimizer.warning_level'
echo "== -Q skips the configuration files:"
"$BIN" -Q -c 'koshconf get mood; koshconf get editor.auto_close_brackets_and_quotes'
echo "== XDG_CONFIG_HOME replaces the default directory:"
XDG_CONFIG_HOME="$home/elsewhere" "$BIN" -c 'koshconf get mood'

echo "== KOSHCONF applies after the file and leaves the environment:"
KOSHCONF=AQEBBQEA "$BIN" -c 'koshconf get mood; koshconf get editor.auto_close_brackets_and_quotes
env | grep -c "^KOSHCONF="'
echo "== an invalid KOSHCONF is a warning:"
KOSHCONF='%%%' "$BIN" -c 'koshconf get mood'
echo "rc=$?"

echo "== the kosh mood sources no rc file:"
printf 'echo koshrc-ran\n' >"$home/.koshrc"
printf 'mood=kosh\n' >"$conf"
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c koshrc-ran
echo "== an interactive kosh mood warns once about a retired koshrc:"
printf 'set -L kosh\nset -L kosh\n' | "$BIN" -i 2>&1 |
  grep 'no longer read' | sed -e "s|$home|HOME|" -e "s|' and '/etc/koshrc'|'|"
echo "== -Q and a non-interactive shell do not warn:"
"$BIN" -Q -i <"$TEST_NULL_DEVICE" 2>&1 | grep -c 'no longer read'
"$BIN" -c : 2>&1 | grep -c 'no longer read'
echo "== a configured bash mood sources the bash rc:"
printf 'echo bashrc-ran\n' >"$home/.bashrc"
printf 'mood=bash\n' >"$conf"
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
echo "== an inherited KOSH_HISTORY_SIZE is the editor.history.max_entries option:"
printf 'mood=kosh\n' >"$conf"
KOSH_HISTORY_SIZE=77 "$BIN" -c 'koshconf get editor.history.max_entries; (koshconf get editor.history.max_entries) & wait "$!"'
echo "== an inherited history variable wins over the file, and neither is exported by default:"
printf 'editor.history.file_path=/from-file\neditor.history.max_entries=88\n' >"$conf"
(
  unset KOSH_HISTORY_FILE KOSH_HISTORY_SIZE
  "$BIN" -c 'echo "$KOSH_HISTORY_FILE $KOSH_HISTORY_SIZE"
env | grep -c "^KOSH_HISTORY_" || :'
  KOSH_HISTORY_FILE=/inherited KOSH_HISTORY_SIZE=5 "$BIN" -c '
echo "$(koshconf get editor.history.file_path) $(koshconf get editor.history.max_entries)"
env | grep "^KOSH_HISTORY_"'
)
echo "== an inherited KOSH is replaced and no longer exported:"
KOSH=/elsewhere KOSH_VERSION=0 "$BIN" -c '[ "$KOSH" != /elsewhere ] && echo replaced
env | grep -c -e "^KOSH=" -e "^KOSH_VERSION=" || :
sh -c "test -z \"\$KOSH\" && echo unset-in-a-child-shell"'
echo "== privileged mode ignores and removes KOSHCONF and still reads the file:"
printf 'editor.auto_close_brackets_and_quotes=on\n' >"$conf"
KOSHCONF=BQEA "$BIN" -p -c 'koshconf get editor.auto_close_brackets_and_quotes
env | grep -c "^KOSHCONF="'
KOSHCONF=BQEA "$BIN" -c 'koshconf get editor.auto_close_brackets_and_quotes'
echo "== semantic options from the file apply to scripts, after the mood:"
printf 'interpreter.exit_on_command_failure=on\ninterpreter.glob_includes_dotfiles=on\nmood=bash\n' >"$conf"
printf 'false\necho unreached\n' >"$home/script.sh"
"$BIN" "$home/script.sh"
echo "rc=$?"
"$BIN" -c 'shopt dotglob; set -M'
echo "== a kosh mood value that conflicts with a fixed option is skipped:"
printf 'interpreter.unset_variable_is_error=off\ninterpreter.glob_no_match_expands_to_nothing=off\n' >"$conf"
"$BIN" -c 'koshconf get interpreter.unset_variable_is_error' 2>&1 |
  sed "s|$home|HOME|"
echo "== an unknown name in the file suggests a close one:"
printf 'editor.history.max_entry=3\n' >"$conf"
"$BIN" -c 'koshconf get editor.history.max_entries' 2>&1 | sed "s|$home|HOME|"
echo "== a command-line flag wins over a semantic option from the file:"
printf 'mood=bash\ninterpreter.unset_variable_is_error=off\n' >"$conf"
"$BIN" -u -c 'koshconf get interpreter.unset_variable_is_error'

echo "== the editor and trace options apply from the file:"
printf 'completion.on_tab=off\neditor.highlight_syntax_and_show_ghost_text=off\noptimizer.show_source_traces=off\n' >"$conf"
"$BIN" -c 'koshconf get completion.on_tab
koshconf get editor.highlight_syntax_and_show_ghost_text
koshconf get optimizer.show_source_traces'
echo "== KOSHCONF carries them:"
"$BIN" -c 'export KOSHCONF; XDG_CONFIG_HOME="$1" "$KOSH" -c "koshconf get completion.on_tab; koshconf get optimizer.show_source_traces"' \
  sh "$home/elsewhere"
echo "== a file without traces drops the trace rows:"
printf 'f() { missing_command_xyz; }\nf\n' >"$home/lib.sh"
"$BIN" -c '. "$1"' sh "$home/lib.sh" 2>&1 | grep -c 'trace:'
"$BIN" -Q -c '. "$1"' sh "$home/lib.sh" 2>&1 | grep -c 'trace:'
echo "== the command-line flags win over the file:"
printf 'completion.on_tab=on\neditor.highlight_syntax_and_show_ghost_text=on\noptimizer.show_source_traces=on\n' >"$conf"
"$BIN" -T --no-syntax-highlighting --no-traces -c 'koshconf get completion.on_tab
koshconf get editor.highlight_syntax_and_show_ghost_text
koshconf get optimizer.show_source_traces'
"$BIN" --dumb -c 'koshconf get completion.on_tab'

echo "== init_moods selects the startup files of an interactive shell:"
printf 'init_moods=bash\n' >"$conf"
"$BIN" -c 'koshconf get init_moods; koshconf get mood'
"$BIN" -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
echo "== a script ignores it and keeps the kosh identity:"
"$BIN" -c 'echo "bash version: ${BASH_VERSION:-unset}"'
echo "== -L wins over the file:"
"$BIN" -L kosh -i <"$TEST_NULL_DEVICE" 2>/dev/null | grep -c bashrc-ran
"$BIN" -L sh,bash -c 'koshconf get init_moods'
echo "== an unknown mood in the list is a warning:"
printf 'init_moods=bash,zsh\n' >"$conf"
"$BIN" -c 'koshconf get init_moods' 2>&1 | sed "s|$home|HOME|"
echo "== koshconf set validates the list:"
: >"$conf"
"$BIN" -c 'koshconf set init_moods sh,bash-posix; koshconf get init_moods
koshconf set init_moods ksh' 2>&1 | grep -v '^ '
