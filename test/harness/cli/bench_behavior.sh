# shellcheck disable=unassigned-variable-read
unset KOSH_FLAGS
export KOSH_FLAGS=--no-diagnostics
d=$(mktemp -d)
trap 'test -n "$d" && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT
BENCH_ECHO=/bin/echo
export BENCH_ECHO
# bench forks a kosh process per sample and runs the command under it. Only the
# deterministic lines are checked here, since the timing summary varies per run.
echo "== bench stops on a non-zero exit code:"
"$BIN" -c 'bench --runs 5 false' 2>&1 | grep -E 'exited with status|note:'
echo "== the failure sets the exit status:"
"$BIN" -c 'bench --runs 5 false' >/dev/null 2>&1; echo "rc=$?"
echo "== --ignore-exit-code keeps sampling a failing command:"
"$BIN" -c 'bench --runs 3 --ignore-exit-code false' 2>&1 | grep 'Benchmark:'
echo "== --no-shell --show-output forks the command directly:"
direct_output=$("$BIN" -c \
    'bench --runs 2 --no-shell --show-output "$BENCH_ECHO direct"' 2>&1)
printf '%s\n' "$direct_output" | grep -E '^direct$|Benchmark:'
echo "== invalid values carry granular carets:"
"$BIN" -c 'bench --runs nope true' 2>&1
"$BIN" -c 'bench --runs=nope true' 2>&1
"$BIN" -c 'bench --runs 0 true' 2>&1
"$BIN" -c 'bench --runs 100001 true' 2>&1
"$BIN" -c 'bench --runs 18446744073709551616 true' 2>&1
"$BIN" -c 'bench --duration 18446744073710 true' 2>&1
"$BIN" -c 'bench "--runs=nope" true' 2>&1
"$BIN" -c 'runs=nope; bench --runs=$runs true' 2>&1
echo "== counter capability keeps one complete sample:"
counter_output=$("$BIN" -c \
    'bench --runs 1 --no-shell --show-output "$BENCH_ECHO counter-run"' 2>&1)
test "$(printf '%s\n' "$counter_output" | grep -c '^counter-run$')" -eq 1
counter_row_count=$(printf '%s\n' "$counter_output" | grep -Ec '^  (cpu cycles|instructions|cache refs|cache misses|branch misses)')
case "$counter_row_count" in
    0) ;;
    5)
        cpu_cycle_row=$(printf '%s\n' "$counter_output" | grep '^  cpu cycles')
        instruction_row=$(printf '%s\n' "$counter_output" | grep '^  instructions')
        case "$cpu_cycle_row" in *[1-9]*) ;; *) exit 1 ;; esac
        case "$instruction_row" in *[1-9]*) ;; *) exit 1 ;; esac
        ;;
    *) exit 1 ;;
esac
if [ "$(uname -s)" = Linux ] && command -v perf >/dev/null 2>&1 &&
    perf stat -e cycles,instructions,cache-references,cache-misses,branch-misses \
        -- /bin/true >/dev/null 2>&1; then
    test "$counter_row_count" -eq 5
fi
case "$counter_output" in
    *"Benchmark: \`$BENCH_ECHO counter-run\` (1 run)"*) ;;
    *) exit 1 ;;
esac
echo "counter capability passed"
echo "== counter fallback keeps one complete sample:"
if [ "$(uname -s)" = Linux ]; then
    fallback_output=$(
        "$BIN" -c \
            'ulimit -n 8; bench --runs 1 --no-shell --show-output "$BENCH_ECHO fallback-run"' \
            2>&1
    )
    test "$(printf '%s\n' "$fallback_output" | grep -c '^fallback-run$')" -eq 1
    test "$(printf '%s\n' "$fallback_output" | grep -Ec '^  (cpu cycles|instructions|cache refs|cache misses|branch misses)')" -eq 0
    case "$fallback_output" in
        *"Benchmark: \`$BENCH_ECHO fallback-run\` (1 run)"*) ;;
        *) exit 1 ;;
    esac
fi
echo "counter fallback passed"
echo "== --extended adds resource rows and the default output omits them:"
extended_labels='cpu usage|user time|system time|waiting time|waiting share|scheduler wait|time slices|cycles|instructions|wakeups|energy|page-ins|peak commit|storage read|storage written|voluntary switches|involuntary switches|minor faults|major faults|page faults|block input|block output|read calls|write calls|bytes read|bytes written|other io calls'
plain_output=$("$BIN" -c 'bench --runs 2 --no-shell "$BENCH_ECHO plain"' 2>&1)
plain_extended_count=$(printf '%s\n' "$plain_output" |
    grep -E "^  ($extended_labels)( \*)?  " | grep -Evc '^  instructions( \*)?  ')
test "$plain_extended_count" -eq 0 || { echo "plain output has extended rows"; exit 1; }
extended_output=$("$BIN" -c \
    'bench --runs 2 --extended --no-shell "$BENCH_ECHO extended"' 2>&1)
extended_labels_found=$(printf '%s\n' "$extended_output" |
    sed -n '/^  cpu usage  /,$p' | grep -E "^  ($extended_labels)( \*)?  " | sed 's/^  //; s/  .*//; s/ \*$//' | tr '\n' ',')
case "$extended_labels_found" in
    cpu\ usage,user\ time,system\ time,waiting\ time,waiting\ share,*) ;;
    *) echo "missing extended rows: $extended_labels_found"; exit 1 ;;
esac
case "$(uname -s)" in
    Linux) required_rows='voluntary switches,minor faults,read calls,bytes written,' ;;
    Darwin) required_rows='storage read,minor faults,' ;;
    *) required_rows='' ;;
esac
old_ifs=$IFS
IFS=,
for required_row in $required_rows; do
    case ",$extended_labels_found" in
        *",$required_row,"*) ;;
        *) echo "missing $required_row in: $extended_labels_found"; exit 1 ;;
    esac
done
IFS=$old_ifs
wrapped_output=$("$BIN" -c \
    'bench --runs 2 --extended "$BENCH_ECHO wrapped"' 2>&1)
case "$(uname -s)" in
    Linux|Darwin)
        printf '%s\n' "$wrapped_output" |
            grep -q '^  \* Counts only the measured process' ||
            { echo "missing process-only note: $wrapped_output"; exit 1; }
        printf '%s\n' "$wrapped_output" | grep -Eq '^  [a-z -]+ \*  ' ||
            { echo "no marked process-only row: $wrapped_output"; exit 1; }
        ;;
esac
# A virtual machine reports no cycles, and bench hides a row without a value.
if [ "$(uname -s)" = Darwin ] && [ "$(uname -m)" = arm64 ] &&
    [ "$(sysctl -n kern.hv_vmm_present 2>/dev/null)" != 1 ]; then
    printf '%s\n' "$plain_output" | grep -q '^  cpu cycles' ||
        { echo "missing default cycles: $plain_output"; exit 1; }
fi
ptrace_scope=$(cat /proc/sys/kernel/yama/ptrace_scope 2>/dev/null || echo 0)
if [ "$(uname -s)" = Linux ] && [ "$ptrace_scope" != 3 ]; then
    printf '%s\n' "$extended_output" | grep -Eq '^  syscalls( \(traced\))?  ' ||
        { echo "missing syscall count: $extended_output"; exit 1; }
    printf '%s\n' "$extended_output" | grep -q '^  average active tasks (traced)  ' ||
        { echo "missing active task average: $extended_output"; exit 1; }
fi
extended_row_count=$(printf '%s\n' "$extended_output" |
    grep -Ec '^  [a-z ]+  +[0-9.]+[A-Za-z]* +\+/- ')
plain_row_count=$(printf '%s\n' "$plain_output" |
    grep -Ec '^  [a-z ]+  +[0-9.]+[A-Za-z]* +\+/- ')
test "$extended_row_count" -gt "$plain_row_count" ||
    { echo "extended adds no rows"; exit 1; }
echo "extended rows passed"
echo "== a vanished command fails later with status 127 on its own row:"
cat > "$d/vanishing-environment" <<'SH'
koshkit sleep 0.03 || exit 1
rm -f "$BENCH_VANISHING_COMMAND" || exit 1
SH
vanishing_command="$d/vanishing-command.exe"
ln -s "$BIN" "$vanishing_command"
has_typescript=0
if script -qec true /dev/null >/dev/null 2>&1; then
    has_typescript=1
    NO_COLOR= TERM=xterm BIN=$BIN BASH_ENV="$d/vanishing-environment" \
        BENCH_VANISHING_COMMAND="$vanishing_command" script -qec \
        "$BIN -c 'bench --runs 3 --no-shell \"$vanishing_command --mood bash -c true\"'" \
        "$d/typescript" >/dev/null 2>&1
elif script -q /dev/null /usr/bin/true </dev/null >/dev/null 2>&1; then
    has_typescript=1
    NO_COLOR= TERM=xterm BIN=$BIN BASH_ENV="$d/vanishing-environment" \
        BENCH_VANISHING_COMMAND="$vanishing_command" \
        script -q "$d/typescript" "$BIN" -c \
        "bench --runs 3 --no-shell \"$vanishing_command --mood bash -c true\"" \
        >/dev/null 2>&1
else
    : > "$d/typescript"
fi
terminal_hex=$(od -An -tx1 "$d/typescript" | tr -d ' \n')
if [ "$has_typescript" -eq 1 ]; then
    [ -n "$terminal_hex" ] || exit 1
    [ ! -e "$vanishing_command" ] && [ ! -L "$vanishing_command" ] || exit 1
    # The visible rows drop styles, keep what follows the last line clear,
    # and keep what follows the last carriage return.
    esc=$(printf '\033')
    cr=$(printf '\r')
    typescript_text=$(sed -e "s/$esc\\[[0-9;]*m//g" -e "s/.*$esc\\[2K//" \
        -e "s/$cr\$//" -e "s/.*$cr//" < "$d/typescript")
    vanishing_row=$(printf '%s\n' "$typescript_text" |
        grep -F "$vanishing_command --mood bash -c true" |
        grep -F 'exited with status 127.')
    case "$vanishing_row" in
        -c:1:*) ;;
        *) exit 1 ;;
    esac
fi
echo "vanished command check complete"
echo "== short flags, profile, and warmup:"
"$BIN" -c 'bench -p -N true' 2>&1 | sed -n '1s/^Benchmark: .* (\(.*\))$/\1/p'
"$BIN" -c "bench -n 2 -w3 'echo x >> $d/warmup-count'" >/dev/null 2>&1
wc -l < "$d/warmup-count" | tr -d ' '
"$BIN" -c 'bench -p -n 2 true' 2>&1 | sed -n '1s/^.*error: //p'
"$BIN" -c 'bench -w0 true' 2>&1 | sed -n '1s/^.*error: //p'
"$BIN" -c 'bench -E -n 2 -N true' 2>&1 | grep -c '^  user time  '
echo "== the extended comparison lists more rows than the default one:"
default_rows=$("$BIN" -c 'bench -n 2 true :' 2>&1 |
    sed -n '/^Relative to/,$p' | grep -c '^  [a-z]')
extended_rows=$("$BIN" -c 'bench -E -n 2 true :' 2>&1 |
    sed -n '/^Relative to/,$p' | grep -c '^  [a-z]')
if [ "$extended_rows" -gt "$default_rows" ]; then
    echo "extended comparison grows"
else
    echo "extended rows=$extended_rows default rows=$default_rows"
fi
