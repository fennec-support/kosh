#!/bin/bash
# With the posix option on, a syntax error in an eval or a dot script ends a
# non-interactive shell with status 2, as bash --posix does, and a subshell or
# command substitution ends with it alone. The command builtin takes the
# special builtin property away, so the error then fails only that command.
set -o posix
bad=$(mktemp)
trap 'rm -f "$bad"' EXIT
echo 'if then' >"$bad"
command eval 'fi'
echo "command-eval=$?"
command . "$bad"
echo "command-dot=$?"
(
  eval 'fi'
  echo unreachable
)
echo "subshell-eval=$?"
(
  . "$bad"
  echo unreachable
)
echo "subshell-dot=$?"
x=$(eval 'fi'; echo unreachable)
echo "substitution=[$x] $?"
runs_eval() {
  eval 'fi'
  echo unreachable
}
(runs_eval)
echo "function=$?"
(
  eval "eval 'fi'"
  echo unreachable
)
echo "nested=$?"
trap 'echo "exit-action-saw=$?"; rm -f "$bad"' EXIT
eval 'if then'
echo unreachable
