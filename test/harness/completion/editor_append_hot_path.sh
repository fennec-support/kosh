d=$(mktemp -d)
trap 'test -n "$d" && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT
script_command=$(command -v script)
expect_command=$(command -v expect || :)
RCFILE="$d/editor-rc"
export RCFILE
printf '%s\n' \
    "PS1='> '" \
    "PROMPT_COMMAND='printf ready > \"\$EDITOR_READY_FILE\"; unset PROMPT_COMMAND'" \
    "trap 'echo exited >> \"\$EDITOR_EXIT_FILE\"' EXIT" \
    > "$RCFILE"
EDITOR_EXIT_FILE="$d/editor-exits"
export EDITOR_EXIT_FILE
EDITOR_TTY_FILE="$d/editor-tty"
export EDITOR_TTY_FILE

fail()
{
  echo "failed at line $1" >&2
  exit 1
}

if "$script_command" --version >/dev/null 2>&1; then
    script_style=gnu
else
    script_style=bsd
fi

run_editor()
{
    transcript=$1
    columns=${2-80}
    EDITOR_OPTIONS=${EDITOR_OPTIONS-}
    export EDITOR_OPTIONS
    script_error=$transcript.script-error
    recorder_status=0
    if [ "$script_style" = gnu ]; then
        "$script_command" -q -c \
            "/bin/stty cols $columns rows 24; /usr/bin/tty > \"\$EDITOR_TTY_FILE\"; ENV=\"\$RCFILE\" exec \"\$BIN\" -i -L sh \${EDITOR_OPTIONS-}" \
            "$transcript" >/dev/null 2>"$script_error" || recorder_status=$?
    elif [ -n "$expect_command" ]; then
        TRANSCRIPT="$transcript" COLUMNS="$columns" "$expect_command" -c '
            # A sanitized editor on a machine running the whole suite answers a
            # key far more slowly than one running alone. Both bounds are named
            # once and reported by name.
            set idle_seconds 45
            set wall_seconds 180
            set timeout $idle_seconds
            log_user 0
            log_file -noappend $env(TRANSCRIPT)
            spawn -noecho /bin/sh -c "/bin/stty cols $env(COLUMNS) rows 24; /usr/bin/tty > \"$env(EDITOR_TTY_FILE)\"; ENV=\"$env(RCFILE)\" exec \"$env(BIN)\" -i -L sh $env(EDITOR_OPTIONS)"
            set editor_pid [exp_pid]
            set wall_timeout [after [expr {$wall_seconds * 1000}] {
                puts stderr "editor recorder exceeded $wall_seconds seconds"
                catch {exec /bin/kill -KILL $editor_pid}
                close
                wait
                exit 1
            }]
            set editor_eof 0
            interact -nobuffer "\n" { after 200 } \
                -nobuffer "\003" { after 200 } timeout $idle_seconds {
                puts stderr "editor recorder input was idle for $idle_seconds seconds"
                catch {exec /bin/kill -KILL $editor_pid}
                close
                wait
                exit 1
            } eof { return } -o eof {
                set editor_eof 1
                return
            }
            if {!$editor_eof} {
                expect {
                    eof {}
                    timeout {
                        puts stderr "editor recorder output was idle for $idle_seconds seconds"
                        catch {exec /bin/kill -KILL $editor_pid}
                        close
                        wait
                        exit 1
                    }
                }
            }
            after cancel $wall_timeout
            set editor_wait [wait]
            if {[llength $editor_wait] != 4 || [lindex $editor_wait 2] != 0 || [lindex $editor_wait 3] != 0} {
                puts stderr "editor exited unsuccessfully: $editor_wait"
                exit 1
            }
        ' >/dev/null 2>"$script_error" || recorder_status=$?
    else
        "$script_command" -q /dev/null /bin/sh -c \
            "/bin/stty cols $columns rows 24; /usr/bin/tty > \"\$EDITOR_TTY_FILE\"; ENV=\"\$RCFILE\" exec \"\$BIN\" -i -L sh \${EDITOR_OPTIONS-}" \
            >"$transcript" 2>"$script_error" || recorder_status=$?
    fi
    if [ "$recorder_status" -ne 0 ]; then
        [ ! -s "$script_error" ] || /bin/cat "$script_error"
        return "$recorder_status"
    fi

    return 0
}

wait_for_raw_terminal()
{
    attempt_count=0
    while :; do
        editor_tty=$(cat "$EDITOR_TTY_FILE" 2>/dev/null)
        if [ -n "$editor_tty" ] &&
            /bin/stty -a < "$editor_tty" 2>/dev/null | grep -q -- -icanon
        then
            return 0
        fi
        [ "$attempt_count" -lt 1000 ] || return 1
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
}

wait_for_editor()
{
    ready_file=$1
    attempt_count=0
    while ! grep -F ready "$ready_file" >/dev/null 2>&1; do
        [ "$attempt_count" -lt 1000 ] || return 1
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
    wait_for_raw_terminal
}

wait_for_prompt_count()
{
    ready_file=$1
    expected_prompt_count=$2
    attempt_count=0
    while [ "$(grep '^ready$' "$ready_file" 2>/dev/null | wc -l | tr -d ' ')" \
        -lt "$expected_prompt_count" ]; do
        [ "$attempt_count" -lt 3000 ] || return 1
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
    wait_for_raw_terminal
}

wait_for_marker_count()
{
  marker_file=$1
  expected_marker_count=$2
  attempt_count=0
  while :; do
    marker_count=0
    if [ -f "$marker_file" ]; then
      marker_count=$(wc -l < "$marker_file")
    fi
    [ "$marker_count" -lt "$expected_marker_count" ] || return 0
    [ "$attempt_count" -lt 1000 ] || return 1
    sleep 0.01
    attempt_count=$((attempt_count + 1))
  done
}

finish_editor_input()
{
  exit_count=0
  if [ -f "$EDITOR_EXIT_FILE" ]; then
    exit_count=$(wc -l < "$EDITOR_EXIT_FILE")
  fi
  printf 'exit 0\n'
  wait_for_marker_count "$EDITOR_EXIT_FILE" $((exit_count + 1)) || return 1
  sleep 0.1
}

mkdir "$d/path"
printf '#!/bin/sh\n' > "$d/path/probe-alpha"
printf '#!/bin/sh\n' > "$d/path/probe-beta"
chmod +x "$d/path/probe-alpha" "$d/path/probe-beta"

send_typing_input()
{
    wait_for_prompt_count "$d/typing-ready" 1 || fail "$LINENO"
    for character in e c h o ' ' h e l l o; do
        printf %s "$character"
        sleep 0.02
    done
    printf '\n'
    wait_for_prompt_count "$d/typing-ready" 2 || fail "$LINENO"
    for character in p r o b e; do
        printf %s "$character"
        sleep 0.02
    done
    printf '\n'
    wait_for_prompt_count "$d/typing-ready" 3 || fail "$LINENO"
    finish_editor_input || fail "$LINENO"
}

printf '%s\n' \
    "PS1='> '" \
    "PROMPT_COMMAND='printf \"ready\\\\n\" >> \"\$EDITOR_READY_FILE\"'" \
    "trap 'echo exited >> \"\$EDITOR_EXIT_FILE\"' EXIT" \
    > "$d/typing-rc"
send_typing_input | TERM=xterm-256color PATH="$d/path" \
    EDITOR_READY_FILE="$d/typing-ready" \
    KOSH_HISTORY_FILE="$d/typing-history" RCFILE="$d/typing-rc" BIN="$BIN" \
    run_editor "$d/typing-typescript" || fail "$LINENO"

strings "$d/typing-typescript" | grep -q '^hello$' || fail "$LINENO"
echo 'interactive typing runs a command'

send_unhighlighted_input()
{
    wait_for_editor "$d/unhighlighted-ready" || fail "$LINENO"
    printf 'probe\n'
    sleep 0.2
    printf 'probe-\t\n'
    sleep 0.2
    finish_editor_input || fail "$LINENO"
}

printf '%s\n' \
    "koshconf set editor.completion_menu_style plain" \
    "PS1='> '" \
    "PROMPT_COMMAND='printf ready > \"\$EDITOR_READY_FILE\"; unset PROMPT_COMMAND'" \
    "trap 'echo exited >> \"\$EDITOR_EXIT_FILE\"' EXIT" \
    > "$d/unhighlighted-rc"
send_unhighlighted_input | TERM=xterm-256color PATH="$d/path" \
    EDITOR_OPTIONS=--no-syntax-highlighting \
    EDITOR_READY_FILE="$d/unhighlighted-ready" \
    KOSH_HISTORY_FILE="$d/unhighlighted-history" RCFILE="$d/unhighlighted-rc" \
    BIN="$BIN" run_editor "$d/unhighlighted-typescript" || fail "$LINENO"

strings "$d/unhighlighted-typescript" | grep -q probe-alpha || fail "$LINENO"
echo 'disabled highlighting defers PATH work until TAB'

history_index=0
while [ "$history_index" -lt 32 ]; do
    printf 'zzzz-invalid-history-command-%s\n' "$history_index"
    history_index=$((history_index + 1))
done > "$d/miss-history"

send_history_input()
{
    wait_for_editor "$d/history-ready" || fail "$LINENO"
    printf 'zz\n'
    sleep 0.2
    printf 'zzzzzzz\n'
    sleep 0.2
    finish_editor_input || fail "$LINENO"
}

send_history_input | TERM=xterm-256color PATH="$d/path" \
    EDITOR_READY_FILE="$d/history-ready" \
    KOSH_HISTORY_FILE="$d/miss-history" BIN="$BIN" \
    run_editor "$d/history-typescript" || fail "$LINENO"

if strings "$d/history-typescript" | grep -q zzzz-invalid-history-command; then
    printf 'a history entry with an unresolvable command was suggested\n'
    strings "$d/history-typescript" || true
    exit 1
fi
echo 'history ghost hides entries whose command no longer resolves'

printf 'probe-alpha --ghost-accepted\n' > "$d/accept-history"

send_accepted_history_input()
{
    wait_for_editor "$d/accept-ready" || fail "$LINENO"
    printf 'probe-alpha -'
    sleep 0.3
    printf '\003'
    sleep 0.2
    finish_editor_input || fail "$LINENO"
}

send_accepted_history_input | TERM=xterm-256color PATH="$d/path" \
    EDITOR_READY_FILE="$d/accept-ready" \
    KOSH_HISTORY_FILE="$d/accept-history" BIN="$BIN" \
    run_editor "$d/accept-typescript" || fail "$LINENO"

strings "$d/accept-typescript" | grep -q ghost-accepted || {
    printf 'a history entry with a resolvable command was not suggested\n'
    strings "$d/accept-typescript" || true
    [ ! -s "$d/accept-typescript.script-error" ] ||
        /bin/cat "$d/accept-typescript.script-error"
    exit 1
}
echo 'history ghost suggests entries whose command resolves'

mkdir "$d/menu-bin"
cat > "$d/menu-bin/tailscale" <<'SH'
#!/bin/sh
printf '%s\n' \
    'SUBCOMMANDS' \
    '  alpha        Keep this first long completion description intact' \
    '  beta         Keep this second long completion description intact'
SH
chmod +x "$d/menu-bin/tailscale"

send_menu_input()
{
    wait_for_editor "$d/menu-ready" || fail "$LINENO"
    printf 'tailscale \t'
    sleep 1
    printf '\025'
    finish_editor_input || fail "$LINENO"
}

send_menu_input | ASAN_OPTIONS=detect_stack_use_after_return=1 \
    EDITOR_READY_FILE="$d/menu-ready" MANPATH= \
    PATH="$d/menu-bin${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" \
    KOSH_HISTORY_FILE="$d/menu-history" BIN="$BIN" \
    run_editor "$d/menu-typescript" 100 || fail "$LINENO"

strings "$d/menu-typescript" | \
    grep -q 'Keep this first long completion description intact' || fail "$LINENO"
strings "$d/menu-typescript" | \
    grep -q 'Keep this second long completion description intact' || fail "$LINENO"
echo 'completion menu keeps callback-owned strings alive'

mkdir "$d/retry-bin"
printf '%s\n' \
  '#!/bin/sh' \
  'printf "attempted\\n" >> "$KOSH_HELP_MARKER"' \
  'sleep 10' \
  > "$d/retry-bin/act"
chmod +x "$d/retry-bin/act"

send_help_retry_input()
{
  wait_for_prompt_count "$d/help-retry-ready" 1 || fail "$LINENO"
  printf 'act --mark\t'
  wait_for_marker_count "$d/help-retry-marker" 2 || fail "$LINENO"
  sleep 9
  printf '\t'
  sleep 0.2
  printf '\003'
  sleep 0.2
  printf 'exit 0\n'
}

printf '%s\n' \
  "koshconf set editor.completion_menu_style plain" \
  "PS1='> '" \
  "PROMPT_COMMAND='printf \"ready\\\\n\" >> \"\$EDITOR_READY_FILE\"'" \
  > "$d/help-retry-rc"
send_help_retry_input | TERM=xterm-256color \
  PATH="$d/retry-bin${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" \
  KOSH_HELP_MARKER="$d/help-retry-marker" \
  EDITOR_READY_FILE="$d/help-retry-ready" \
  KOSH_HISTORY_FILE="$d/help-retry-history" RCFILE="$d/help-retry-rc" \
  EDITOR_OPTIONS=--no-syntax-highlighting BIN="$BIN" \
  run_editor "$d/help-retry-typescript" || fail "$LINENO"

test "$(wc -l < "$d/help-retry-marker")" -eq 2 || fail "$LINENO"
echo 'timed out help completion stops after two attempts'

send_help_adopt_input()
{
  wait_for_prompt_count "$d/help-adopt-ready" 1 || fail "$LINENO"
  printf 'act --mark\t'
  wait_for_marker_count "$d/help-adopt-marker" 1 || fail "$LINENO"
  printf '\t\025exit 0\n'
}

send_help_adopt_input | TERM=xterm-256color \
  PATH="$d/retry-bin${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" \
  KOSH_HELP_MARKER="$d/help-adopt-marker" \
  EDITOR_READY_FILE="$d/help-adopt-ready" \
  KOSH_HISTORY_FILE="$d/help-adopt-history" RCFILE="$d/help-retry-rc" \
  EDITOR_OPTIONS=--no-syntax-highlighting BIN="$BIN" \
  run_editor "$d/help-adopt-typescript" || fail "$LINENO"

test "$(wc -l < "$d/help-adopt-marker")" -eq 1 || fail "$LINENO"
echo 'help completion adopts the idle load of the same help'

mkdir "$d/manpath-bin" "$d/recovered-man"
mkdir "$d/recovered-man/man1"
printf '#!/bin/sh\n' > "$d/manpath-bin/koshmanprobe"
printf '%s\n' \
  '#!/bin/sh' \
  'printf "attempted\\n" >> "$KOSH_MANPATH_MARKER"' \
  'if [ ! -f "$KOSH_MANPATH_ATTEMPTED" ]; then' \
  '  : > "$KOSH_MANPATH_ATTEMPTED"' \
  '  sleep 10' \
  'else' \
  '  printf "%s\\n" "$KOSH_MANPATH_ROOT"' \
  'fi' \
  > "$d/manpath-bin/manpath"
chmod +x "$d/manpath-bin/koshmanprobe" "$d/manpath-bin/manpath"
printf '%s\n' '.TH KOSHMANPROBE 1' '.SH SYNOPSIS' '\fBkoshmanprobe\fR' \
  > "$d/recovered-man/man1/koshmanprobe.1"
printf '%s\n' '.TH KOSHMANPROBE-RECOVERED 1' '.SH SYNOPSIS' \
  '\fBkoshmanprobe\fR \fBrecovered\fR' \
  > "$d/recovered-man/man1/koshmanprobe-recovered.1"

send_manpath_retry_input()
{
  wait_for_prompt_count "$d/manpath-ready" 1 || fail "$LINENO"
  printf 'koshmanprobe rec\t'
  wait_for_marker_count "$d/manpath-marker" 2 || fail "$LINENO"
  sleep 1
  printf '\n'
  wait_for_prompt_count "$d/manpath-ready" 2 || fail "$LINENO"
  printf 'exit 0\n'
}

printf '%s\n' \
  "koshconf set editor.completion_menu_style plain" \
  "PS1='> '" \
  "PROMPT_COMMAND='printf \"ready\\\\n\" >> \"\$EDITOR_READY_FILE\"'" \
  > "$d/manpath-rc"
send_manpath_retry_input | TERM=xterm-256color MANPATH= \
  PATH="$d/manpath-bin${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" \
  KOSH_MANPATH_MARKER="$d/manpath-marker" \
  KOSH_MANPATH_ATTEMPTED="$d/manpath-attempted" \
  KOSH_MANPATH_ROOT="$d/recovered-man" \
  EDITOR_READY_FILE="$d/manpath-ready" KOSH_HISTORY_FILE="$d/manpath-history" \
  RCFILE="$d/manpath-rc" BIN="$BIN" \
  run_editor "$d/manpath-typescript" || fail "$LINENO"

test "$(wc -l < "$d/manpath-marker")" -eq 2 || fail "$LINENO"
strings "$d/manpath-typescript" | grep -q recovered || fail "$LINENO"
echo 'man subcommand indexing recovers after a timed out manpath command'

mkdir "$d/quoted-completion"
touch "$d/quoted-completion/space name" \
    "$d/quoted-completion/plain name" \
    "$d/quoted-completion/README-one" \
    "$d/quoted-completion/ReadMe-two"

send_quoted_input()
{
    wait_for_editor "$d/quoted-ready" || fail "$LINENO"
    printf "cd '%s'\n" "$d/quoted-completion"
    sleep 0.2
    printf "printf '<%%s>\\\\n' 'spX'\033[D\033[D\t\n"
    sleep 0.2
    printf "printf '<%%s>\\\\n' plaX\033[D\t\n"
    sleep 0.2
    printf "COMPLETION_PROBE=variable-value\n"
    sleep 0.2
    printf "printf '<%%s>\\\\n' \"\$COMPLETION_PROBX\"\033[D\033[D\t\n"
    sleep 0.2
    printf "printf '<%%s>\\\\n' read\tone\n"
    sleep 0.2
    finish_editor_input || fail "$LINENO"
}

send_quoted_input | EDITOR_READY_FILE="$d/quoted-ready" \
    KOSH_HISTORY_FILE="$d/quoted-history" BIN="$BIN" \
    run_editor "$d/quoted-typescript" || fail "$LINENO"

for quoted_expected_output in \
    '<space name>' '<plain name>' '<variable-value>' '<README-one>'
do
    strings "$d/quoted-typescript" | grep -q "$quoted_expected_output" || {
        printf 'quoted completion output missing: %s\n' "$quoted_expected_output"
        strings "$d/quoted-typescript" || true
        [ ! -s "$d/quoted-typescript.script-error" ] ||
            /bin/cat "$d/quoted-typescript.script-error"
        exit 1
    }
done
echo 'quoted replacement and smart-case TAB preserve the completed token'

send_navigation_input()
{
    wait_for_prompt_count "$d/navigation-ready" 1 || fail "$LINENO"
    printf 'echo alt-left-XXX\033[1;3Dfixed-\n'
    wait_for_prompt_count "$d/navigation-ready" 2 || fail "$LINENO"
    printf 'echo ctrl-left-XXX\033[1;5Dfixed-\n'
    wait_for_prompt_count "$d/navigation-ready" 3 || fail "$LINENO"
    printf 'echo XXX tail\001\033[C\033[C\033[C\033[C\033[C\033[1;3C-fixed\n'
    wait_for_prompt_count "$d/navigation-ready" 4 || fail "$LINENO"
    printf 'echo XXX tail\001\033[C\033[C\033[C\033[C\033[C\033[1;5C-fixed\n'
    wait_for_prompt_count "$d/navigation-ready" 5 || fail "$LINENO"
    printf 'cho command-home\033[He\n'
    wait_for_prompt_count "$d/navigation-ready" 6 || fail "$LINENO"
    printf 'echo command-end\033[H\033[F-ok\n'
    wait_for_prompt_count "$d/navigation-ready" 7 || fail "$LINENO"
    printf 'echo option-del remove\033\177kept\n'
    wait_for_prompt_count "$d/navigation-ready" 8 || fail "$LINENO"
    printf 'echo option-bs remove\033\010kept\n'
    wait_for_prompt_count "$d/navigation-ready" 9 || fail "$LINENO"
    printf 'echo ctrl-w remove\027kept\n'
    wait_for_prompt_count "$d/navigation-ready" 10 || fail "$LINENO"
    printf 'exit 0\n'
}

printf '%s\n' \
    "PS1='> '" \
    "PROMPT_COMMAND='printf \"ready\\\\n\" >> \"\$EDITOR_READY_FILE\"'" \
    > "$d/navigation-rc"
send_navigation_input | TERM=xterm-256color \
    EDITOR_READY_FILE="$d/navigation-ready" \
    KOSH_HISTORY_FILE="$d/navigation-history" RCFILE="$d/navigation-rc" BIN="$BIN" \
    run_editor "$d/navigation-typescript" || fail "$LINENO"

for navigation_expected_output in \
    'alt-left-fixed-XXX' 'ctrl-left-fixed-XXX' \
    'XXX-fixed tail' 'command-home' 'command-end-ok' \
    'option-del kept' 'option-bs kept' 'ctrl-w kept'
do
    strings "$d/navigation-typescript" | \
        grep -q "$navigation_expected_output" || fail "$LINENO"
done
echo 'Alt and Ctrl keys preserve word and line editing'

printf '#!/bin/sh\nprintf "actual-cwd-completion\\n"\n' > "$d/actual-cwd-probe"
chmod +x "$d/actual-cwd-probe"
actual_cwd_completion=$(
    cd "$d" && PWD=invalid "$BIN" --debug-complete-at './actual-cwd-'
)
printf '%s\n' "$actual_cwd_completion" | grep -q actual-cwd-probe || fail "$LINENO"
echo 'clobbered PWD completion uses the actual directory'
