# With progcomp_alias on, a command with no spec of its own borrows the spec
# of its alias target. A TAB also follows a PATH symlink to the target, but the
# ghost runs on every key and stays in memory: it follows the alias and never
# searches PATH, so a symlink name gets no spec ghost.
dir=$(mktemp -d)
mkdir "$dir/commands"
printf '#!/bin/sh\n' > "$dir/commands/target"
chmod +x "$dir/commands/target"
ln -s target "$dir/commands/probe"
setup="shopt -s progcomp_alias; complete -W 'target_word' target"

ghost()
{
  env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir/commands" \
    "$BIN" -c "$1" --debug-ghost-at "$2" </dev/null | grep -E '^(count|prefix)='
}

listing()
{
  env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$dir/commands" \
    "$BIN" -c "$1" --debug-complete-at "$2" </dev/null
}

echo "== the ghost follows an alias to the target spec:"
ghost "alias pa='target -x'; $setup" 'pa tar'
echo "== a TAB follows an alias to the target spec:"
listing "alias pa='target -x'; $setup" 'pa tar'
echo "== the ghost does not follow a PATH symlink:"
ghost "$setup" 'probe tar'
echo "== a TAB follows a PATH symlink to the target spec:"
listing "$setup" 'probe tar'
test -n "$dir" && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"
