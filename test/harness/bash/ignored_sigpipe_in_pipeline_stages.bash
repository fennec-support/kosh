#!/bin/bash
# When trap '' ignores SIGPIPE, a pipeline stage keeps it ignored. A builtin or
# program that writes to a closed pipe reports a write error and continues.
# Clearing the trap restores the default, and the stage exits with status 141,
# as in bash.
trap '' PIPE
{ for ((i = 1; i <= 100000; i++)); do echo "$i" || { echo "stopped" >&2; break; }; done; } | head -1
echo "builtin-ignored=${PIPESTATUS[*]}"
( sleep 0.2; echo late; echo "subshell=$?" >&2 ) | true
yes | head -1
echo "program-ignored=${PIPESTATUS[*]}"
trap - PIPE
{ for ((i = 1; i <= 100000; i++)); do echo "$i"; done; } | head -1
echo "builtin-default=${PIPESTATUS[*]}"
yes | head -1
echo "program-default=${PIPESTATUS[*]}"

# trap '' INT keeps SIGINT ignored in a subshell and in a program.
trap '' INT
( kill -INT $BASHPID; echo "subshell-ignores-int=$?" )
/bin/sh -c 'kill -INT $$; echo "program-ignores-int=$?"'
trap - INT

# Without job control, an asynchronous command starts with SIGINT ignored.
/bin/sh -c 'kill -INT $$; echo "async-ignores-int=$?"' &
wait $!
echo "async-status=$?"
