set -e

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
if [ "${OS-}" = Windows_NT ]; then
    executable_name=explicit-probe.exe
else
    executable_name=EXPLICIT-PROBE.EXE
fi
printf '#!/bin/sh\n' > "$dir/$executable_name"
chmod +x "$dir/$executable_name"
printf '#!/bin/sh\n' > "$dir/collision.com"
chmod +x "$dir/collision.com"
mkdir "$dir/directory.exe"
if [ "${OS-}" = Windows_NT ]; then
    mkdir "$dir/foo"
else
    : > "$dir/foo"
fi
printf '#!/bin/sh\n' > "$dir/foobar"
chmod +x "$dir/foobar"

env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-highlight-at \
    'EXPLICIT-PROBE.EXE; collision.exe; directory.exe' |
    grep -E ' :role (resolved-command|partial-command|unknown-command)\)$'
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-highlight-at 'EXPLICIT-P' |
    grep -E ' :role partial-command\)$'
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-highlight-at 'explicit-p' |
    grep -E ' :role partial-command\)$'
env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir" \
    "$BIN" --debug-highlight-at 'foo' |
    grep -E ' :role partial-command\)$'

mkdir "$dir/blocker-first" "$dir/blocker-second"
mkdir "$dir/blocker-first/blocked.exe"
printf '#!/bin/sh\n' > "$dir/blocker-second/blocked.exe"
chmod +x "$dir/blocker-second/blocked.exe"
env -u PATH \
    "$TEST_PATH_ENVIRONMENT_NAME=$dir/blocker-first${TEST_PATH_SEPARATOR}$dir/blocker-second" \
    "$BIN" --debug-highlight-at 'blocked.exe' |
    grep -E ' :role resolved-command\)$'

PATH=/nonexistent "$BIN" --debug-highlight-at 'npr' |
    grep -E ' :role partial-command\)$'
PATH=/nonexistent "$BIN" --debug-highlight-at 'ec\ho' |
    grep -E ' :role resolved-command\)$'
mkdir "$dir/existing"
(
    cd "$dir"
    PATH=/nonexistent "$BIN" --debug-highlight-at 'ech>./existing/out'
) | grep -E ' :role (unknown-command|operator|existing-path|invalid-path)\)$'

: > "$dir/existing/kosprobe"
(
    cd "$dir/existing"
    "$BIN" --debug-highlight-at './kos'
) | grep -E ' :role (existing-path|partial-path)\)$'
"$BIN" --debug-highlight-at '~' | grep -Fx '(span "~" :role partial-path)'

if [ "${OS-}" != Windows_NT ]; then
    mkdir "$dir/between"
    printf '#!/bin/sh\n' > "$dir/between/gone-probe"
    printf '#!/bin/sh\n' > "$dir/between/blocked-probe"
    chmod +x "$dir/between/gone-probe" "$dir/between/blocked-probe"
    BETWEEN_DIRECTORY="$dir/between" \
        PATH="$dir/between" "$BIN" \
        --debug-highlight-at 'appear-probe; gone-probe; blocked-probe' -c '
        compgen -c >/dev/null
        : > "$BETWEEN_DIRECTORY/appear-probe"
        /bin/chmod +x "$BETWEEN_DIRECTORY/appear-probe"
        /bin/rm "$BETWEEN_DIRECTORY/gone-probe"
        /bin/chmod -x "$BETWEEN_DIRECTORY/blocked-probe"
        :' |
        grep -E ' :role (resolved-command|unknown-command)\)$'
else
    printf '(span "appear-probe" :role resolved-command)\n'
    printf '(span "gone-probe" :role unknown-command)\n'
    printf '(span "blocked-probe" :role unknown-command)\n'
fi
