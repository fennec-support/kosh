# The meow screensaver draws one still cat when its output is not a terminal,
# so a redirected run ends at once and carries no escape sequences.
unset KOSH_FLAGS
echo "== redirected output receives one still cat:"
"$BIN" -c 'koshkit meow; printf "status=%s\n" "$?"' </dev/null
echo "== the frame rate option is accepted, and a rate of zero is refused:"
"$BIN" -c 'koshkit meow --fps 30 | koshkit wc -l' </dev/null
"$BIN" -c 'koshkit meow -f 0 2>/dev/null; printf "status=%s\n" "$?"' </dev/null
echo "== an operand is refused:"
"$BIN" -c 'koshkit meow purr 2>/dev/null; printf "status=%s\n" "$?"' </dev/null
