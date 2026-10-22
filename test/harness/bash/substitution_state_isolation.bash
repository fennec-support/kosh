#!/bin/bash
# Changes made inside a command substitution or a subshell do not reach the
# parent shell: aliases, PATH, the working directory, the directory stack,
# set and shopt options, traps, positional parameters, completion
# specifications, and remembered command locations. Each probe prints the
# visible state after the isolated body ran.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/a" "$dir/b"
printf '#!/bin/sh\necho probe-a\n' > "$dir/a/stateprobe"
chmod +x "$dir/a/stateprobe"
start=$PWD
sys=$PATH

alias keep='echo kept'
complete -W 'one two' keepcmd
set -- p1 p2
trap 'echo parent-exit' USR1
shopt -u nullglob
set +o noclobber

show() {
  echo "-- $1"
  shift
  alias | sort
  echo "path-restored=$([ "$PATH" = "$sys" ] && echo yes || echo no)"
  echo "cwd-restored=$([ "$PWD" = "$start" ] && echo yes || echo no)"
  echo "dirs=$(dirs -p | wc -l)"
  echo "noclobber=$(set -o | awk '$1 == "noclobber" { print $2 }')"
  echo "nullglob=$(shopt -p nullglob)"
  echo "params=$#:$*"
  trap -p USR1
  complete -p | sort
  type -P stateprobe >/dev/null && echo probe-found || echo probe-missing
  hash -t stateprobe 2>/dev/null || echo not-hashed
}

show initial "$@"

x=$(
  alias gone='echo gone'
  unalias keep
  PATH=$dir/a:$sys
  stateprobe
  hash stateprobe
  cd "$dir/b"
  pushd "$dir/a" >/dev/null
  set -o noclobber
  shopt -s nullglob
  set -- changed
  trap 'echo inner' USR1
  complete -W three keepcmd
  complete -W extra other
  echo done
)
echo "substitution=$x"
show "after substitution" "$@"

(
  alias gone='echo gone'
  unalias keep
  PATH=$dir/a:$sys
  stateprobe >/dev/null
  hash stateprobe
  cd "$dir/b"
  pushd "$dir/a" >/dev/null
  set -o noclobber
  shopt -s nullglob
  set -- changed
  trap - USR1
  complete -r keepcmd
  complete -W extra other
)
show "after subshell" "$@"

for i in 1 2 3; do
  x=$(PATH=$dir/a:$sys; stateprobe; hash -r; alias loop=1; cd "$dir/a")
  y=$(stateprobe 2>/dev/null || echo missing)
  echo "loop $i: $x $y"
done
show "after loop" "$@"

PATH=$dir/a:$sys
x=$(stateprobe)
echo "warm in parent: $x"
y=$(PATH=$sys; stateprobe 2>/dev/null || echo missing)
echo "lookup inside: $y"
z=$(stateprobe)
echo "lookup after: $z"
PATH=$sys

x=$(hash -p /bin/true stateprobe; hash -t stateprobe)
echo "hashed inside: $x"
hash -t stateprobe 2>/dev/null || echo "not hashed after"

PATH=$dir/a:$sys
hash -r
x=$(stateprobe)
hash -t stateprobe 2>/dev/null || echo "not hashed after lookup inside"
stateprobe
hash -t stateprobe | sed "s|$dir|DIR|"
PATH=$sys
