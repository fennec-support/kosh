unset KOSH_FLAGS
# In the bash mood, a {var} redirection whose variable cannot take the
# descriptor number closes the file it opened, so repeated failures leave the
# descriptor table as it was.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
"$BIN" --mood bash -c 'readonly target=1
before=$(koshkit ls /dev/fd | koshkit wc -l)
for attempt in 1 2 3 4 5; do
  true {target}> "$1/out"
done
after=$(koshkit ls /dev/fd | koshkit wc -l)
printf "unchanged=%s\n" "$([ "$before" = "$after" ] && echo yes)"' kosh "$work" \
  2>/dev/null
