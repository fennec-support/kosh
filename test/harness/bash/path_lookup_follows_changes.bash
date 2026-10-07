#!/bin/bash
# Every way of changing PATH moves command lookup with it, and every
# temporary change is undone where the shell undoes it. Two directories hold
# a probe of the same name that prints which directory it came from.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/a" "$dir/b"
printf '#!/bin/sh\necho a\n' > "$dir/a/pathprobe"
printf '#!/bin/sh\necho b\n' > "$dir/b/pathprobe"
chmod +x "$dir/a/pathprobe" "$dir/b/pathprobe"
sys=$PATH
A=$dir/a:$sys
B=$dir/b:$sys

PATH=$A
echo "assign: $(pathprobe)"
PATH=$B
echo "reassign: $(pathprobe)"
pathprobe
hash pathprobe
PATH=$A
echo "hashed reassign: $(pathprobe)"
type -P pathprobe | sed "s|$dir|DIR|"
command -v pathprobe | sed "s|$dir|DIR|"

PATH=$B pathprobe
echo "after prefix: $(pathprobe)"
PATH=$B command pathprobe
echo "after command prefix: $(pathprobe)"
PATH=$B eval pathprobe
echo "after eval prefix: $(pathprobe)"
pathprobe

show() { pathprobe; }
PATH=$B show
echo "after function prefix: $(pathprobe)"
pathprobe

with_local() {
  local PATH=$B
  pathprobe
}
with_local
echo "after local: $(pathprobe)"
pathprobe

nested_local() {
  local PATH=$B
  inner() { local PATH=$A; pathprobe; }
  inner
  pathprobe
}
nested_local
echo "after nested local: $(pathprobe)"

declare_local() {
  declare PATH=$B
  pathprobe
}
declare_local
echo "after declare local: $(pathprobe)"

(PATH=$B; pathprobe)
echo "after subshell: $(pathprobe)"
echo "substitution: $(PATH=$B; pathprobe)"
echo "after substitution: $(pathprobe)"
{ PATH=$B; pathprobe; } | cat
echo "after pipeline stage: $(pathprobe)"

export PATH=$B
echo "export: $(pathprobe)"
declare -x PATH=$A
echo "declare -x: $(pathprobe)"
read -r PATH <<< "$B"
echo "read: $(pathprobe)"
printf -v PATH '%s' "$A"
echo "printf -v: $(pathprobe)"
PATH=$dir/b
PATH+=":$sys"
echo "append: $(pathprobe)"
for PATH in "$A" "$B"; do
  pathprobe
done
echo "after loop: $(pathprobe)"

unset PATH
pathprobe 2>/dev/null
echo "unset status: $?"
PATH=$A
echo "after unset: $(pathprobe)"

PATH=
pathprobe 2>/dev/null
echo "empty status: $?"
PATH=$B:$dir/a:$B
echo "duplicate entries: $(pathprobe)"
type -a -P pathprobe | sed "s|$dir|DIR|"

cd "$dir/a" || exit 1
unset PATH
pathprobe
command -v pathprobe
echo "unset in directory: $(pathprobe)"
PATH=
echo "empty in directory: $(pathprobe)"
PATH=:
echo "colon in directory: $(pathprobe)"
PATH=/nonexistent:
echo "trailing colon: $(pathprobe)"
PATH=:/nonexistent
echo "leading colon: $(pathprobe)"
PATH=/nonexistent::/nonexistent
echo "inner empty: $(pathprobe)"
PATH=/nonexistent
pathprobe 2>/dev/null
echo "no directory status: $?"
unset PATH
{ PATH=/nonexistent; } | :
echo "after stage over unset: $(pathprobe)"
PATH=$sys
cd / || exit 1
