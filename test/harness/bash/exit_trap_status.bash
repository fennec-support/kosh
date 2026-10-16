#!/bin/bash

# The EXIT trap reads the status the shell is ending with, checked against
# bash. The operand of the exit builtin reaches the action through $?, and an
# operand outside one byte range is masked before the action reads it.

echo before_trap

"$BASH" -c 'trap "echo \"action_saw=\$?\"; echo action_tail; exit" EXIT; n=300; echo last_body; exit "$n"'
echo "masked_status=$?"

returns_seven() {
  return 7
}
returns_seven
echo "function_status=$?"

trap 'echo "action_saw=$?"; echo action_tail' EXIT

echo last_body
exit 42
