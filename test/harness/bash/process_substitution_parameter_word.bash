#!/bin/bash
# shellcheck disable=SC2086
# A process substitution in the word of a default, alternate, assign, or error
# parameter expansion expands to its path unless the word or the expansion is
# quoted, checked against bash.
show() { printf '%s\n' "$*" | sed 's#/dev/fd/[0-9]*#<path>#g'; }
unset x
cat ${x:-<(echo from-default)}
cat ${x-<(echo from-unset-default)}
show 1 ${x:-<(echo a)}
show 2 ${x:-pre<(echo a)post}
show 3 ${x:->(cat)}
show 4 ${x:-a<(echo a)b<(echo b)}
show 5 ${x:-${x:-<(echo nested)}}
show 6 ${x:-<(echo "a b")}
x=set
cat ${x:+<(echo from-alternate)}
show 7 ${x:+<(echo a)}
unset x
show 8 ${x:+<(echo a)}.
y=""
show 9 ${y-<(echo a)}.
show 10 ${y:-<(echo a)}
unset z
show 11 ${z:=<(echo a)}
show 12 "$z"
cat "$z" 2>/dev/null
echo "assigned-path-closed=$?"
unset z
: ${z:=x<(echo a)}
show 13 "$z"
w=${x:-<(echo a)}
show 14 "$w"
f=$(mktemp)
for p in ${x:-<(echo looped)}; do cat "$p"; done >"$f"
cat "$f"
rm -f "$f"
show 15 "${x:-<(echo a)}"
show 16 ${x:-'<(echo a)'}
show 17 ${x:-"<(echo a)"}
show 18 ${x:-\<(echo a)}
show 19 ${x:-"<(echo a)"x<(echo b)}
show 20 "${x=<(echo a)}"
show 21 "$x"
unset x
v=abc
show 22 ${x:-"$v"<(echo a)}
show 23 ${x:-$(echo '<(echo a)')}
( : ${x?<(echo a)} ) 2>/dev/null
echo "error-status=$?"
