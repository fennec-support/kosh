# The ghost runs a completion spec on every key, so it keeps only the
# generators the shell answers from memory: the word list and the alias,
# builtin, function, variable, and other in-memory actions. The user, group,
# hostname, service, command, file, and directory actions, the glob, and the
# directory options wait for a TAB. A -I spec with no
# such generator, such as one with only a function, leaves the command-name
# ghost alone. The probe runs in an empty directory with an empty PATH.
dir=$(mktemp -d)
cd "$dir" || exit
mkdir "$dir/commands"

ghost()
{
  env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir/commands" \
    "$BIN" -c "$1" --debug-ghost-at "$2" </dev/null | grep -E '^(count|prefix)='
}

echo "== the table actions offer no ghost:"
ghost "complete -A user -A group -A hostname -A service foo" 'foo r'
echo "== the word list still fills the ghost beside a table action:"
ghost "complete -A user -W 'alpha' foo" 'foo al'
echo "== the alias action still fills the ghost:"
ghost "alias myalias=true; complete -A alias -A user foo" 'foo mya'
echo "== a -I spec with only a function keeps the command-name ghost:"
ghost "f() { COMPREPLY=(x); }; complete -I -F f" 'complet'
echo "== a -I spec with a word list fills the ghost:"
ghost "complete -I -W 'initial_one'" 'ini'
cd /
rm -rf "$dir"
