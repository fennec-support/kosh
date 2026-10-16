# The compound keywords are transparent to command position, so the body of
# an if, while, until, then, else, elif, do, or brace group completes
# commands, and their argument stages ride through to the inner command. A
# case pattern's unmatched closing paren opens the arm's body the same way,
# while a matched paren closes a substitution and stays an argument. PATH is
# pinned to an empty directory so a host binary such as exportfs cannot join the
# command candidates. A command on a later physical line of a multi-line buffer
# completes as its own command, and a cd there offers only directories. A
# separator inside quotes is not a command boundary, so the argument still
# completes against the filesystem.
dir=$(mktemp -d)
trap 'cd / && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
mkdir "$dir/adir"
: > "$dir/afile"
: > "$dir/ZQmarker_file"
mkdir "$dir/ZQdir_marker"
export PATH="$dir"
cd "$dir"
echo "== if body:"
"$BIN" --debug-complete-at 'if expor' </dev/null
echo "== then body:"
"$BIN" --debug-complete-at 'if [ -f x ]; then expor' </dev/null
echo "== while body:"
"$BIN" --debug-complete-at 'while expor' </dev/null
echo "== do body:"
"$BIN" --debug-complete-at 'for f in a b; do expor' </dev/null
echo "== brace group:"
"$BIN" --debug-complete-at '{ expor' </dev/null
echo "== arguments through the keyword:"
"$BIN" --debug-complete-at 'while set -o no' </dev/null
echo "== case arm body:"
"$BIN" --debug-complete-at 'case $x in a) expor' </dev/null
echo "== case arm with alternates:"
"$BIN" --debug-complete-at 'case $x in a|b) expor' </dev/null
echo "== arguments through the case arm:"
"$BIN" --debug-complete-at 'case $x in a) set -o no' </dev/null
echo "== matched paren stays an argument:"
"$BIN" --debug-complete-at 'echo $(ls) expor' </dev/null
echo "== quoted separator stays an argument:"
"$BIN" --debug-complete-at 'echo "a;b=c" expor' </dev/null
echo "== quoted assignment prefix:"
"$BIN" --debug-complete-at 'FOO="a b" expor' </dev/null
echo "== arguments through a quoted assignment prefix:"
"$BIN" --debug-complete-at 'FOO="a b" set -o no' </dev/null
echo "== subshell body:"
"$BIN" --debug-complete-at '( expor' </dev/null
echo "== spec command on the first line:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at 'svc ' </dev/null
echo "== spec command after a newline:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at "$(printf 'echo hi\nsvc ')" </dev/null
echo "== spec command after '&&' then a newline:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at "$(printf 'true &&\nsvc ')" </dev/null
echo "== spec command in a loop body on the next line:"
"$BIN" -c 'complete -W "start stop restart" svc' --debug-complete-at "$(printf 'for x in a b; do\nsvc ')" </dev/null
echo "== cd offers only directories on the next line:"
"$BIN" --debug-complete-at "$(printf 'echo hi\ncd ')" </dev/null
echo "== plain argument lists both markers:"
"$BIN" --debug-complete-at 'echo ZQ' </dev/null
echo "== a quoted semicolon does not split the segment:"
"$BIN" --debug-complete-at 'echo "a;b" ZQ' </dev/null
echo "== a quoted pipe does not split the segment:"
"$BIN" --debug-complete-at "echo 'a|b' ZQ" </dev/null
echo "== a quoted ampersand does not split the segment:"
"$BIN" --debug-complete-at 'echo "a&b" ZQ' </dev/null
echo "== a real separator still starts a new command segment:"
"$BIN" --debug-complete-at 'true; echo ZQ' </dev/null
