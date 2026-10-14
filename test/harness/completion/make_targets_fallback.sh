# The make target completion scans the Makefile with the bundled parser and
# never starts a make program. A Makefile can run commands while make loads it,
# and completion must not run them.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf 'all: build\n\ttrue\nbuild:\n\ttrue\ntest: build\n\ttrue\nclean:\n\ttrue\n.PHONY: clean\n' > "$dir/Makefile"
cd "$dir"

cat > "$dir/make" <<'SH'
#!/bin/sh
: > probe-ran
exec >/dev/null 2>&1
exec sleep 30
SH
chmod +x "$dir/make"
echo "== make targets with a make program on PATH:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" -c \
  'koshkit timeout -k 1s 10s "$1" --debug-complete-at "make "' \
  shell "$BIN" </dev/null
printf 'completion-status=%s\n' "$?"
if [ -f probe-ran ]; then
  echo probe-started
else
  echo probe-not-started
fi
