# shellcheck disable=unassigned-variable-read
unset KOSH_FLAGS
# A script operand under mimicry reads its own shebang. `kosh -I script.sh`
# analyzes and runs the file under the mood its interpreter names, the same
# mood the dispatch path picks for the script run as a command.
directory=$(mktemp -d)
trap '[ -n "$directory" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$directory"' EXIT

printf '#!/bin/bash\nset -M\n' > "$directory/bash-script"
printf '#!/bin/sh\nset -M\n' > "$directory/sh-script"
printf '#!/usr/bin/env dash\nset -M\n' > "$directory/env-script"
printf 'set -M\n' > "$directory/plain-script"
printf '#!/bin/bash\necho "[${undefined_name}]"\n' > "$directory/unset-script"

echo "== a bash shebang selects the bash mood:"
"$BIN" --no-traces -I "$directory/bash-script"
echo "== an sh shebang selects the posix mood:"
"$BIN" --no-traces -I "$directory/sh-script"
echo "== an env shebang selects the named shell:"
"$BIN" --no-traces -I "$directory/env-script"
echo "== a script with no shebang keeps the session mood:"
"$BIN" --no-traces -I "$directory/plain-script"
echo "== a script operand reads its shebang without -I:"
"$BIN" --no-traces "$directory/bash-script"
echo "== an explicit --mood wins:"
"$BIN" --no-traces -I --mood kosh "$directory/bash-script"
echo "== --dumb wins:"
"$BIN" --no-traces -I --dumb "$directory/bash-script"
echo "== an sh invocation name wins:"
"$BIN" --no-traces -c 'exec -a sh "$1" --no-traces -I "$2"' \
    shell "$BIN" "$directory/bash-script"

echo "== the mimicked mood relaxes the strict options:"
"$BIN" --no-traces -I "$directory/unset-script"
echo "rc=$?"

echo "== an explicit -u stays fatal:"
"$BIN" --no-traces -I -u "$directory/unset-script" \
    > "$directory/strict-output" 2>&1
strict_status=$?
sed 's|\\|/|g' "$directory/strict-output" | sed "s|$directory|TMPDIR|g"
echo "rc=$strict_status"

echo "== the strict default still reports the unset read:"
"$BIN" --no-traces -M kosh "$directory/unset-script" \
    > "$directory/default-output" 2>&1
default_status=$?
sed 's|\\|/|g' "$directory/default-output" | sed "s|$directory|TMPDIR|g"
echo "rc=$default_status"

printf '#!/usr/bin/env bash\n[[ x == x ]] && echo ok\na=(1 2)\necho "${a[1]}"\necho $(echo unquoted)\n' \
    > "$directory/bash-syntax"
printf '#!/usr/bin/env -S bash -e\necho $(echo split)\n' > "$directory/env-split"
printf '#!/bin/sh\n[[ x == x ]] && echo ok\n' > "$directory/sh-syntax"

echo "== a bash shebang runs bash syntax without -I:"
"$BIN" --no-traces "$directory/bash-syntax"
echo "rc=$?"
echo "== an env -S shebang selects the named shell:"
"$BIN" --no-traces "$directory/env-split"
echo "rc=$?"
echo "== an sh shebang rejects the bash conditional:"
"$BIN" --no-traces "$directory/sh-syntax" 2>&1 |
    sed "s|$directory|TMPDIR|g" | head -n 1
echo "== -M kosh keeps the strict analysis on a bash shebang:"
"$BIN" --no-traces -M kosh "$directory/bash-syntax" > /dev/null 2>&1
echo "rc=$?"
