# A -C command runs on an explicit tab with the command name, the word, and
# the previous word appended, and with COMP_LINE, COMP_POINT, COMP_KEY, and
# COMP_TYPE exported. Each nonempty output line is one candidate, taken as
# printed and then filtered, prefixed, and suffixed like the other sources.
# The ghost never runs the command. The probe runs in a temp directory
# holding the generator, which records its arguments and environment.
dir=$(mktemp -d)
printf '%s\n' '#!/bin/sh' \
  'printf "%s\n" "$COMP_LINE|$COMP_POINT|$COMP_KEY|$COMP_TYPE|${COMP_CWORD-unset}" >ran.txt' \
  'printf "[%s]\n" "$@" >>ran.txt' \
  'printf "%s\n" xa "xb c" "" zz' >"$dir/gen"
chmod +x "$dir/gen"
echo "== the output lines are the candidates:"
"$BIN" -c "cd '$dir' || exit; complete -C ./gen foo" --debug-complete-at 'foo ab x' </dev/null
echo "== the command sees its arguments and environment:"
cat "$dir/ran.txt"
rm -f "$dir/ran.txt"
echo "== -X, -P, and -S apply to the output:"
"$BIN" -c "cd '$dir' || exit; complete -C ./gen -X 'z*' -P '<' -S '>' foo" --debug-complete-at 'foo x' </dev/null
rm -f "$dir/ran.txt"
echo "== the command text takes its own arguments first:"
"$BIN" -c "cd '$dir' || exit; complete -C 'printf \"%s\n\"' foo" --debug-complete-at 'foo a' </dev/null
echo "== the ghost leaves the command alone:"
"$BIN" -c "cd '$dir' || exit; complete -C ./gen foo" --debug-ghost-at 'foo x' </dev/null | grep '^count='
if [ -e "$dir/ran.txt" ]; then echo "the ghost ran the command"; else echo "the ghost ran nothing"; fi
echo "== complete -p prints -C between -X and -F:"
"$BIN" -c "complete -W 'w' -X 'x' -C 'printf %s\\\\n \"\$1\"' -F fn foo; complete -p foo"
cd /
rm -rf "$dir"
