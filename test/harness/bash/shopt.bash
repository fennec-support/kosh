#!/bin/bash
# Bash shopt builtin, checked byte-for-byte against bash. Sets, unsets, and
# queries the shell options and the -q exit status, prints the replayable -p
# form, and exercises the options that change lookup, substitution, pipeline,
# and local variable behavior. The display line's padding width differs between
# bash builds, so the listing runs are squeezed through tr and compare on
# content rather than the column.
shopt -s extglob
shopt extglob | tr -s ' \t' ' '
shopt -u extglob
shopt extglob | tr -s ' \t' ' '
shopt -s globstar nullglob dotglob
shopt globstar | tr -s ' \t' ' '
shopt nullglob | tr -s ' \t' ' '
shopt dotglob | tr -s ' \t' ' '
shopt -q extglob
echo "extglob query: $?"
shopt -s extglob
shopt -q extglob
echo "after set: $?"
shopt -u extglob
shopt -q extglob
echo "after unset: $?"
shopt -u xpg_echo
case :$BASHOPTS: in
  *:xpg_echo:*) echo "bashopts-before=on" ;;
  *) echo "bashopts-before=off" ;;
esac
shopt -s xpg_echo
case :$BASHOPTS: in
  *:xpg_echo:*) echo "bashopts-after=on" ;;
  *) echo "bashopts-after=off" ;;
esac
(BASHOPTS=changed)
echo "bashopts-assign=$?"
unset BASHOPTS
echo "bashopts-unset=$?"
case :$BASHOPTS: in
  *:xpg_echo:*) echo "bashopts-preserved=on" ;;
  *) echo "bashopts-preserved=off" ;;
esac
shopt -s nocaseglob nocasematch
shopt nocaseglob | tr -s ' \t' ' '
shopt nocasematch | tr -s ' \t' ' '


shopt -qo posix; echo "posix=$?"
set -e; shopt -qo errexit; echo "e=$?"
set +e; shopt -qo errexit; echo "e2=$?"
shopt -qo nounset; echo "u=$?"
set -u; shopt -qo nounset; echo "u2=$?"
shopt -qs progcomp; echo "shopt-still-works=$?"

set +o interactive-comments
shopt -q interactive_comments; echo "interactive-comments-off=$?"
set -o interactive-comments
shopt -q interactive_comments; echo "interactive-comments-on=$?"
case :$SHELLOPTS: in
  *:interactive-comments:*) echo interactive-comments-listed ;;
  *) echo interactive-comments-missing ;;
esac

alias shopt_alias='echo alias-expanded'
shopt -u expand_aliases
shopt_alias 2>/dev/null; echo "alias-off=$?"
shopt -s expand_aliases
shopt_alias

value=aaab
shopt -s extglob
echo "extglob-on=${value##+(a)}"
shopt -u extglob
echo "extglob-off=${value##+(a)}"

set +u

# shopt -p prints a command the shell replays, shopt -s or -u for a shopt name
# and set -o or +o behind the -o bridge, the $(shopt -p globstar) round-trip
# a completion file performs.
(
  shopt -u globstar extglob nullglob
  shopt -p globstar
  shopt -p extglob nullglob
  shopt -po pipefail
  reset=$(shopt -p globstar)
  shopt -s globstar
  $reset
  shopt globstar | tr -s ' \t' ' '
  shopt -p globstar
)

# The Bash startup history options and the Bash 5.3 spelling are accepted.
shopt -s histappend cmdhist lithist array_expand_once
echo "history-status=$?"
shopt histappend cmdhist lithist assoc_expand_once | tr -s ' \t' ' '

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/on-path" "$dir/cwd" "$dir/first" "$dir/second"

# sourcepath controls whether source searches PATH before the current
# directory.
printf 'printf "path\\n"\n' > "$dir/on-path/sourcepath-probe"
printf 'printf "local\\n"\n' > "$dir/cwd/sourcepath-probe"
(
  cd "$dir/cwd"
  PATH=$dir/on-path:$PATH
  source sourcepath-probe
  printf 'sourcepath-on=%s\n' "$?"
  shopt -u sourcepath
  source sourcepath-probe
  printf 'sourcepath-off=%s\n' "$?"
)

# inherit_errexit controls whether a command substitution keeps errexit.
(
  set -e
  shopt -u inherit_errexit
  value=$(false; printf kept)
  printf 'inherit-off=%s:%s\n' "$?" "$value"
)
echo "inherit-off-rc=$?"
(
  set -e
  shopt -s inherit_errexit
  value=$(false; printf lost)
  printf 'inherit-on=%s:%s\n' "$?" "$value"
)
echo "inherit-on-rc=$?"

# lastpipe runs the final pipeline stage in the current shell unless job
# control is on.
(
  unset value
  printf x | read value
  printf 'default=<%s>\n' "$value"
  shopt -s lastpipe
  set +m
  printf y | read value
  printf 'lastpipe=<%s>\n' "$value"
  set -m
  printf z | read value
  printf 'monitor=<%s>\n' "$value"
  set +m
  printf group | { read value; }
  printf 'group=<%s>\n' "$value"
  read_value() { read value; }
  printf function | read_value
  printf 'function=<%s>\n' "$value"
)

# localvar_inherit gives a bare local the value and attributes of the outer
# variable.
(
  x=outer
  f()
  {
    local x
    printf 'value=<%s> set=%s\n' "$x" "${x+x}"
  }
  f
  shopt -s localvar_inherit
  f
  declare -i integer=7
  integer_test()
  {
    local integer
    integer=2+3
    printf 'integer=%s\n' "$integer"
  }
  integer_test
  export exported=outer
  export_test()
  {
    local exported
    declare -p exported
  }
  shopt -u localvar_inherit
  export_test
  values=(one two)
  array_test()
  {
    local values
    printf 'array=%s\n' "${values[*]}"
  }
  shopt -s localvar_inherit
  array_test
)

# checkhash validates a hashed command before it runs. The first probe moves
# itself away on its first run, so a stale hash entry fails until checkhash
# searches PATH again.
printf '#!/bin/sh\nprintf "first\\n"\nmv "$0" "$0.off"\n' \
  > "$dir/first/checkhash-probe"
printf '#!/bin/sh\nprintf "second\\n"\n' > "$dir/second/checkhash-probe"
chmod +x "$dir/first/checkhash-probe" "$dir/second/checkhash-probe"
(
  PATH=$dir/first:$dir/second:$PATH
  checkhash-probe
  checkhash-probe 2>/dev/null
  printf 'checkhash-off=%s\n' "$?"
  shopt -s checkhash
  checkhash-probe
  printf 'checkhash-on=%s\n' "$?"
)

# The compatibility levels and the Bash 5.3 options are accepted. Each one
# sets, reports, and unsets on its own.
for option in compat31 compat32 compat40 compat41 compat42 compat43 compat44 \
  array_expand_once noexpand_translation bash_source_fullpath; do
  shopt -s "$option"
  echo "$option set=$?"
  shopt -q "$option"
  echo "$option query=$?"
  shopt -u "$option"
  shopt -q "$option"
  echo "$option unset=$?"
done
shopt -p compat42 noexpand_translation bash_source_fullpath
