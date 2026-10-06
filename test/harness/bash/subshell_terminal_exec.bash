#!/bin/bash

# The last command of a forked substitution, subshell, pipeline stage, or
# background subshell keeps its traps, status, and missing-command status.

(/bin/sh -c 'echo subshell ran')

echo x | (/bin/sh -c 'cat > /dev/null; echo pipeline subshell stage ran')

(/bin/sh -c 'echo background subshell ran') &
wait

trapped=$(trap 'echo exit-trap' EXIT; /bin/sh -c 'echo ran')
echo "substitution with exit trap: $trapped"

( trap 'echo err-trap' ERR; /bin/false )
echo "subshell with err trap status=$?"

status=$(/bin/sh -c 'exit 3')
echo "substitution status=$? output=<$status>"

missing=$(/nonexistent/terminal-command 2> /dev/null)
echo "missing command status=$? output=<$missing>"

callbacks=$(printf 'x\ny\n' | { mapfile -t -C /bin/echo -c 1 lines; })
echo "mapfile callbacks in a substitution: $callbacks"
