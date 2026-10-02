#!/bin/bash
# The character count of a stored value stays correct after every write path
# switches it between pure ASCII and multibyte content under a UTF-8 locale,
# checked byte-for-byte against bash. A value is classified once and the answer
# must be dropped by assignment, append, read, printf -v, mapfile, array
# element writes, declare, local, unset, trimming, and growth past the inline
# buffer.

LC_ALL=C.UTF-8

E=$'\xc3\xa9'
U=$'\xc3\xbc'
K=$'\xe6\x97\xa5\xe6\x9c\xac'
J=$'\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e'
long_ascii=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
long_wide=
for _ in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25; do
  long_wide+=$E
done

s=abc
echo "assign:${#s}"
echo "again:${#s}"
s=$E
echo "assign-wide:${#s}"
s=abc
echo "assign-back:${#s}"

s=abc
echo "pre:${#s}"
s+=$E
echo "append-wide:${#s}"
s=abc
echo "pre:${#s}"
s+=def
echo "append-ascii:${#s}"
s+=$U
echo "append-wide:${#s}"

s=$long_ascii
echo "long:${#s}"
s+=$E
echo "long-append-wide:${#s}"
s=$long_wide
echo "long-wide:${#s}"
s=$long_ascii
echo "long-back:${#s}"
s=$long_ascii$long_ascii
echo "long-double:${#s}"
s+=$long_wide
echo "long-grown:${#s}"

s=abc
echo "pre:${#s}"
read -r s <<<"$J"
echo "read-wide:${#s}"
read -r s <<<'abc'
echo "read-ascii:${#s}"

s=abc
echo "pre:${#s}"
printf -v s '%s' "$E"
echo "printf-wide:${#s}"
printf -v s '%s' 'abcd'
echo "printf-ascii:${#s}"
printf -v s '%s%s' "$long_ascii" "$U"
echo "printf-long-wide:${#s}"

s=abc
echo "pre:${#s}"
mapfile -t s <<<"$E"
echo "mapfile-wide:${#s}:${#s[0]}"
mapfile -t s <<<'xyz'
echo "mapfile-ascii:${#s}:${#s[0]}"

a=(abc def)
echo "pre:${#a[0]}:${#a[1]}"
a[0]=$E
a[1]+=$E
echo "element-wide:${#a[0]}:${#a[1]}"
a[0]=abc
echo "element-ascii:${#a[0]}"
a+=("$K")
echo "element-added:${#a[2]}"
mapfile -t a <<<"$E"$'\n'abc
echo "mapfile-array:${#a[0]}:${#a[1]}"

s=abc
echo "pre:${#s}"
declare s=$E
echo "declare-wide:${#s}"
declare s=abc
echo "declare-ascii:${#s}"
declare -g s=$K
echo "declare-global:${#s}"

s=abc
echo "pre:${#s}"
f() {
  local s=$E
  echo "local-wide:${#s}"
  local s=abc
  echo "local-ascii:${#s}"
  s=$J
  echo "local-assign:${#s}"
}
f
echo "restored:${#s}"
s=$E
echo "pre:${#s}"
f
echo "restored-wide:${#s}"

s=$E
echo "pre:${#s}"
unset s
s=abc
echo "unset-ascii:${#s}"
unset s
echo "unset-gone:${#s}"

s=${E}abc
echo "pre:${#s}"
s=${s#"$E"}
echo "trim-prefix:${#s}"
s=abc$E
echo "pre:${#s}"
s=${s%"$E"}
echo "trim-suffix:${#s}"
s=abc$E
s=${s/"$E"/e}
echo "replace:${#s}"
s=abc
s=${s/b/$E}
echo "replace-wide:${#s}"
s=abcd$E
echo "pre:${#s}"
s=${s:0:4}
echo "slice-ascii:${#s}"

s=abc
echo "pre:${#s}"
((n = 1))
s=$n
echo "arith:${#s}"
for s in "$E" abc "$K"; do
  echo "loop:${#s}"
done
case x in x) s=$E ;; esac
echo "case:${#s}"
t=$s
s=abc
echo "copy:${#t}:${#s}"
s=$(printf '%s' "$E")
echo "subst:${#s}"
s=$(printf abc)
echo "subst-ascii:${#s}"

export s=$E
echo "export:${#s}"
export s=abc
echo "export-ascii:${#s}"
readonly r=$E
echo "readonly:${#r}"
