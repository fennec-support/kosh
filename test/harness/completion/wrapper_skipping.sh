# A wrapper command, its dash options, and leading assignments are transparent,
# so the inner command drives both command-position and argument completion,
# the way fish skips sudo. PATH is pinned to an empty directory so a host binary
# such as exportfs cannot join the command candidates.
dir=$(mktemp -d)
trap '[ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
export PATH="$dir"
echo "== command position through sudo:"
"$BIN" --debug-complete-at 'sudo expor' </dev/null
echo "== inner builtin args through env and an assignment:"
"$BIN" --debug-complete-at 'env FOO=bar set -o no' </dev/null
echo "== command position through time options:"
"$BIN" --debug-complete-at 'time -p -R expor' </dev/null
echo "== command position through timeout:"
"$BIN" --debug-complete-at 'timeout -s TERM 1 expor' </dev/null
echo "== quoted and escaped timeout options stay transparent:"
"$BIN" --debug-complete-at "timeout '-s' TERM 1 expor" </dev/null
"$BIN" --debug-complete-at 'timeout \-s TERM 1 expor' </dev/null
"$BIN" --debug-complete-at 'timeout "\-s" TERM 1 expor' </dev/null
echo "== command position through koshkit timeout:"
"$BIN" --debug-complete-at 'koshkit timeout --kill-after=1 2 expor' </dev/null
echo "== builtin offers only builtins for its first operand:"
"$BIN" -c 'export_fn() { :; }' --debug-complete-at 'builtin expor' </dev/null
"$BIN" -c 'export_fn() { :; }' --debug-complete-at 'expor' </dev/null
echo "== builtin stays transparent for the inner builtin arguments:"
"$BIN" --debug-complete-at 'builtin set -o noc' </dev/null
