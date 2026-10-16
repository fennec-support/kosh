unset KOSH_FLAGS
# A return a DEBUG action requests with no function and no sourced file reports
# a diagnostic with the trap source trace, and the traced command still runs.
d=$(mktemp -d) || exit 1
trap '[ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT
"$BIN" --mood bash -c 'set -T
trap '"'"'case "$BASH_COMMAND" in echo\ top*) return 5;; esac'"'"' DEBUG
echo top-1
echo top-2
trap - DEBUG
echo tail' > "$d/captured.out" 2>&1
rc=$?
"invoke-normalize-trace" "$BIN" < "$d/captured.out"; echo "rc=$rc"
