set -e

# With autocd, a directory or a run of dots in command position becomes a cd,
# so it is highlighted as a resolved command.
root=$(mktemp -d)
root=$(cd "$root" && pwd)
trap 'test -n "$root" && rm -rf "$root"' EXIT
mkdir -p "$root/subdir"
cd "$root"

for line in subdir .. ...; do
  "$BIN" --debug-highlight-at "$line" |
    grep -Fx "(span \"$line\" :role resolved-command)"
done

"$BIN" --debug-highlight-at 'missing-directory' |
  grep -Fx '(span "missing-directory" :role unknown-command)'
