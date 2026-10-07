#!/bin/bash
# exec with only redirections keeps a process substitution open for the life of
# the redirected descriptor, checked against bash. The path descriptor closes
# after the exec, so only the redirected number stays open.
exec 3< <(echo from-three)
cat <&3
echo "read-status=$?"
exec 3<&-
exec 5< <(printf 'l1\nl2\n')
read -r x <&5
read -r y <&5
echo "x=$x y=$y"
exec 5<&-
exec 6< <(echo six) 7< <(echo seven)
cat <&7
cat <&6
exec 6<&- 7<&-
f() { exec 8< <(echo in-function); }
f
cat <&8
exec 8<&-
{ exec 9< <(echo braced); }
cat <&9
exec 9<&-
exec 10< <(echo ten)
cat <&10
exec 10<&-

out=$(mktemp)
exec 4> >(sed 's/^/out:/' >"$out")
echo hello >&4
echo "write-status=$?"
exec 4>&-
for _ in $(seq 50); do
  [ -s "$out" ] && break
  sleep 0.1
done
cat "$out"
rm -f "$out"

exec 3< <(echo a)
[ -e /dev/fd/3 ] && echo "redirected-open"
[ -e /dev/fd/63 ] || echo "path-closed"
exec 3<&-
[ -e /dev/fd/3 ] || echo "redirected-closed"
sleep 0.2
:
ps -o stat= --ppid $$ 2>/dev/null | grep -c Z
exec 4> >(cat >/dev/null)
echo done
