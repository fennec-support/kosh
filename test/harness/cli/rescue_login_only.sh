unset KOSH_FLAGS
# A login spawn, marked by a dash-prefixed argv[0], drops to the rescue prompt
# when a flag fails to parse rather than exiting, so a broken login config does
# not lock the user out. A non-login invocation with the same bad flag exits 2
# without a rescue prompt, and a bad flag arriving through KOSH_FLAGS exits 2
# when standard input is not a terminal. The login shell is built through a
# dash-prefixed symlink the way login and tmux start one.
echo "== login shell with a bad flag enters rescue:"
"$BIN" -c 'exec -a -kosh "$1" --nonexistent-flag' \
    test-login "$BIN" <"$TEST_NULL_DEVICE" 2>&1 | grep -c "Entering rescue"
echo "== non-login shell with the same bad flag does not:"
result=$("$BIN" --nonexistent-flag <"$TEST_NULL_DEVICE" 2>&1)
[ "$?" -eq 2 ] || exit 1
case $result in
  *'Entering rescue'*) exit 1 ;;
  *) printf '0\n' ;;
esac
echo "== a bad flag in KOSH_FLAGS exits 2 without rescue:"
# The error reconstructs argv, which carries the binary path, so only the
# path-independent error text and the exit status are printed.
rescue_output=$(KOSH_FLAGS='--nonexistent-flag' "$BIN" -c 'echo unreached' <"$TEST_NULL_DEVICE" 2>&1)
rescue_status=$?
printf '%s\n' "$rescue_output" | grep -o "error: Unknown flag '--nonexistent-flag'."
echo "exit=$rescue_status"
[ "$rescue_status" -eq 2 ] || exit 1
case $rescue_output in
  *'Entering rescue'*) exit 1 ;;
esac
