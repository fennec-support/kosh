#!/bin/sh
# A fatal expansion error inside a trap action ends the shell with status 2,
# rather than only the action, checked against dash. The EXIT action reads
# that status.
trap 'echo exit-action-saw=$?' EXIT
trap 'echo usr1-action; : ${zz?q}; echo after-in-action' USR1
kill -USR1 $$
echo unreachable
