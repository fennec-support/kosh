#!/bin/bash
# declare -g writes the global beneath a local of the same name and leaves
# the local alone. A subshell and a command substitution list the inherited
# EXIT trap without running it, until the subshell changes any trap. An unset
# of a caller's local exposes the outer value unless localvar_unset is on.
gg=0
shadow() {
  declare -g gg=2
  local gg=3
  declare -g gg=4
  echo "local: $gg"
  typeset -g gg+=5
  echo "local again: $gg"
}
shadow
echo "global: $gg"
caller_scope() { local x=1; callee_unset; echo "caller[${x-unset}]"; }
callee_unset() { unset x; echo "callee[${x-unset}]"; }
x=top
caller_scope
shopt -s localvar_unset
caller_scope
echo "top[${x-unset}]"
shopt -u localvar_unset
trap 'echo bye' EXIT
trap 'echo interrupted' INT
( trap 'echo user' USR1; trap -p )
echo ---
( trap - INT; trap -p )
echo ---
saved=$(trap -p EXIT)
echo "[$saved]"
( trap )
trap -p EXIT
