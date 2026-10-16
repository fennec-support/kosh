#!/bin/bash
# The EXIT trap against a shell that errexit ends, checked against bash. The
# action runs, sees the failing status, and its own commands run to the end.
# Each case ends a child shell through a plain command, a function body, a
# pipefail pipeline, or a subshell status.
trap_text='echo "action_saw=$?"; echo action_tail'

run_case()
{
  echo "=== $1"
  "$BASH" -c "trap '$trap_text' EXIT; set -e; $2; echo unreachable"
  echo "status=$?"
}

run_case plain 'echo last_body; false'
run_case function 'f() { echo in_body; /bin/cat /nonexistent-kosh-errexit-probe 2> /dev/null; echo unreachable_body; }; f'
run_case pipefail 'set -o pipefail; echo before; false | /bin/cat'
run_case subshell 'echo before; ( exit 3 )'
