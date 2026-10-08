#!/bin/bash
# A fatal expansion error inside a trap action ends the shell the action runs
# in, rather than only the action, checked against bash. A subshell ends with
# status 1 from a signal or an ERR action, and the script ends at the last one
# without reaching the rest of its body.
(
  trap 'echo usr1-action; : ${zz?q}; echo after-in-action' USR1
  kill -USR1 "$BASHPID"
  echo unreachable
)
echo "signal-subshell=$?"
(
  trap 'echo err-action; : ${zz?q}; echo after-in-action' ERR
  false
  echo unreachable
)
echo "err-subshell=$?"
(
  set -u
  trap 'echo nounset-action; echo "$undefined_name"; echo after-in-action' USR1
  kill -USR1 "$BASHPID"
  echo unreachable
)
echo "nounset-subshell=$?"
trap 'echo exit-action-saw=$?' EXIT
trap 'echo usr1-action; : ${zz?q}; echo after-in-action' USR1
kill -USR1 $$
echo unreachable
