unset KOSH_FLAGS
# fg gives a stopped job the terminal, and the job reads the line typed after
# it resumes. The shell runs under script(1) with tostop set.

BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")

d=$(mktemp -d)
d=$(cd "$d" && pwd -P)
trap 'test -n "$d" && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT

probe="$d/stopped.bash"
printf '%s\n' '#!/bin/bash' \
    'kill -STOP "$BASHPID"' \
    'echo FG_READY' \
    'read value' \
    'echo "FG_READ:$value"' > "$probe"
chmod +x "$probe"

printf 'printf ready > "%s"\n' "$d/ready" > "$d/rc"

wait_for_transcript()
{
    text=$1
    attempt_count=0
    while ! grep -aF "$text" "$d/live" >/dev/null 2>&1; do
        [ "$attempt_count" -lt 1000 ] || return 1
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
}

send_input()
{
    attempt_count=0
    while ! grep -F ready "$d/ready" >/dev/null 2>&1; do
        [ "$attempt_count" -lt 1000 ] || return 1
        sleep 0.01
        attempt_count=$((attempt_count + 1))
    done
    wait_for_transcript 'Bash me harder!' || return 1
    printf 'set -M kosh; echo HANDOFF_STEP_1\n'
    wait_for_transcript 'HANDOFF_STEP_1' || return 1
    printf 'stty tostop; echo HANDOFF_STEP_2\n'
    wait_for_transcript 'HANDOFF_STEP_2' || return 1
    printf "%s 'argument with spaces'\n" "$probe"
    wait_for_transcript 'Stopped' || return 1
    printf 'fg\n'
    wait_for_transcript 'FG_READY' || return 1
    printf 'terminal-value\n'
    wait_for_transcript 'FG_READ:terminal-value' || return 1
    printf 'exit\n'
}

if script -q -c true /dev/null >/dev/null 2>&1; then
    send_input | script -q -c \
        "exec \"$BIN\" -i -I --mood bash --rcfile \"$d/rc\"" \
        "$d/typescript" >"$d/live" 2>/dev/null
elif script -q /dev/null /usr/bin/true </dev/null >/dev/null 2>&1; then
    send_input | script -q "$d/typescript" /bin/sh -c \
        "exec \"$BIN\" -i -I --mood bash --rcfile \"$d/rc\"" \
        >"$d/live" 2>/dev/null
else
    exit 1
fi

if LC_ALL=C grep -aF 'FG_READ:terminal-value' "$d/typescript" \
    >/dev/null 2>&1; then
    echo passed
    exit 0
fi

printf 'terminal_read=%s\n' \
    "$(LC_ALL=C grep -aFc 'FG_READ:terminal-value' "$d/typescript" 2>/dev/null)"
exit 1
