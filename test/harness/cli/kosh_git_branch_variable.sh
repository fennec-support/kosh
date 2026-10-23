unset KOSH_FLAGS
# KOSH_GIT_BRANCH gets the branch the \G prompt segment renders, from
# .git/HEAD without forking git. The probe builds the repository layouts by
# hand, the attached ref, the detached hash, the worktree gitdir pointer,
# and the walk up from a subdirectory, so no git binary is needed.
root=../../kosh-git-branch-${PPID-0}-$$
trap 'test -n "$root" && rm -rf "$root"' EXIT
mkdir -p "$root/repo/.git" "$root/repo/sub/dir"
printf 'ref: refs/heads/probe-branch\n' > "$root/repo/.git/HEAD"
mkdir -p "$root/repo-two/.git"
printf 'ref: refs/heads/second-branch\n' > "$root/repo-two/.git/HEAD"
"$BIN" -c "cd '$root/repo' && echo \"attached=\$KOSH_GIT_BRANCH\""
"$BIN" -c "cd '$root/repo' || exit; echo \"first=\$KOSH_GIT_BRANCH\"; cd ../repo-two || exit; echo \"second=\$KOSH_GIT_BRANCH\""
"$BIN" -c "cd '$root/repo/sub/dir' && echo \"walked=\$KOSH_GIT_BRANCH\""
printf '0123456789abcdef\n' > "$root/repo/.git/HEAD"
"$BIN" -c "cd '$root/repo' && echo \"detached=\$KOSH_GIT_BRANCH\""
mkdir -p "$root/real/gitdir" "$root/tree"
if ! real_gitdir=$(cd "$root/real/gitdir" && pwd -W 2>/dev/null); then
    real_gitdir=$(cd "$root/real/gitdir" && pwd -P)
fi
printf 'gitdir: %s\n' "$real_gitdir" > "$root/tree/.git"
printf 'ref: refs/heads/linked-tree\n' > "$root/real/gitdir/HEAD"
"$BIN" -c "cd '$root/tree' && echo \"worktree=\$KOSH_GIT_BRANCH\""
if ! ceiling=$(cd "$root/.." && pwd -W 2>/dev/null); then
    ceiling=$(cd "$root/.." && pwd -P)
fi
GIT_CEILING_DIRECTORIES=$ceiling "$BIN" -c "cd '$root' && echo \"outside=[\$KOSH_GIT_BRANCH]\""
"$BIN" -c "cd '$root/repo/sub/dir' && GIT_CEILING_DIRECTORIES='$ceiling' && echo \"below-ceiling=\$KOSH_GIT_BRANCH\""
"$BIN" -c "cd '$root/repo' && KOSH_GIT_BRANCH=stored && echo \"stored=\$KOSH_GIT_BRANCH\""
# KOSH_GIT_AHEAD starts git only from a trusted absolute directory, so a git
# planted in the repository and reached through an empty PATH entry never runs.
upstream=$root/upstream
mkdir -p "$upstream/.git/refs/heads" "$upstream/.git/refs/remotes/origin"
printf 'ref: refs/heads/main\n' > "$upstream/.git/HEAD"
printf '1111111111111111111111111111111111111111\n' \
    > "$upstream/.git/refs/heads/main"
printf '2222222222222222222222222222222222222222\n' \
    > "$upstream/.git/refs/remotes/origin/main"
printf '[branch "main"]\n\tremote = origin\n\tmerge = refs/heads/main\n' \
    > "$upstream/.git/config"
printf '#!/bin/sh\n: > planted-ran\necho "3 4"\n' > "$upstream/git"
chmod 755 "$upstream" "$upstream/git"
(cd "$upstream" &&
    PATH=":/nonexistent" "$BIN" -c 'echo "ahead=[${KOSH_GIT_AHEAD-}]"')
if [ -f "$upstream/planted-ran" ]; then echo "planted-git=ran"; else echo "planted-git=not-run"; fi
rm -rf "$root"
echo "rc=$?"
