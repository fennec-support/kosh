#!/bin/bash

# A forked substitution, subshell, pipeline stage, or background subshell runs
# its last external command in place, so that command's parent is the shell
# itself. A trap that has to run after the command keeps the fork, and the
# status and the missing-command status still reach the parent.

self=$$

sub=$(/bin/sh -c 'echo "$PPID"')
[ "$sub" = "$self" ] && echo "substitution execs in place"

list=$(cd / && /bin/sh -c 'echo "$PPID"')
[ "$list" = "$self" ] && echo "substitution list execs its last command"

(/bin/sh -c '[ "$PPID" = "$1" ] && echo "subshell execs in place"' sh "$self")

echo x | (/bin/sh -c 'cat > /dev/null; [ "$PPID" = "$1" ] && echo "pipeline subshell stage execs in place"' sh "$self")

(/bin/sh -c '[ "$PPID" = "$1" ] && echo "background subshell execs in place"' sh "$self") &
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
