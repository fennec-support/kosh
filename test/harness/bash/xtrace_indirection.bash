#!/bin/bash
# The xtrace prefix repeats the first PS4 character once per enclosing command
# substitution, process substitution, eval, source, and trap action. A subshell,
# a group, and a pipeline stage add no depth. The trace goes through sort
# because the stages of a pipeline write in no fixed order. A later stage reads
# a line of the earlier one before it traces, because the trace of Bash is not
# atomic and two stages that trace together can tear a line.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cd "$dir" || exit 1
echo 'echo src' > inc.sh

body() {
  set -x
  f() { echo in f; }
  f | { { set +x; read -r line; } 2>/dev/null; set -x; cat <<<"$line"; }
  { echo grp; } | { { set +x; read -r line; } 2>/dev/null; set -x; cat <<<"$line"; }
  ( echo sub )
  eval 'echo ev'
  eval 'eval "echo ev2"'
  x=$(echo cs)
  y=$(echo $(echo n))
  . ./inc.sh
  # the substitution traces after the outer command line, so the two trace
  # lines cannot interleave.
  cat <({ set +x; sleep 0.5; } 2>/dev/null; set -x; echo ps)
  echo "$(f)"
  eval 'echo $(echo q)'
  ( z=$(echo sub_cs) )
  g() { echo $(echo in g); }
  g
  trap 'echo errtrap' ERR
  false
  trap - ERR
  for i in 1; do echo loop; done
  echo a | { { set +x; read -r line; } 2>/dev/null; set -x; echo b; } |
    { { set +x; read -r line; } 2>/dev/null; set -x; cat <<<"$line"; }
  set +x
}

body 2>&1 | sed 's|/dev/fd/[0-9]*|/dev/fd/N|' | LC_ALL=C sort

# The EXIT trap traces at the level of the shell that runs it, and a mapfile
# callback one level deeper.
(
  set -x
  trap 'echo exit-trap' EXIT
  cb() { :; }
  printf 'start\na\nb\n' | {
    { set +x; read -r line; } 2>/dev/null
    set -x
    mapfile -t -C cb -c 1 lines
  }
) 2>&1 | LC_ALL=C sort

# A word with a control character traces in $'...' form, other words keep
# single quotes, and an array element never takes the $'...' form.
(
  set -x
  : $'a\nb' $'t\tx' '' 'a b' a=b
  y=$'q\tw'
  a=($'\n' z 'p q')
) 2>&1

# Assignments, array assignments, a prefix assignment, a for loop, and a
# redirection trace as the command that runs.
(
  set -x
  scalar=value
  scalar+=tail
  arr=(one apt-get "two three" $scalar)
  empty=()
  arr+=(four)
  PFX=value printf '%s\n' ok
  for i in a b; do : "$i"; done
  printf done >/dev/null
  set +x
) 2>&1
