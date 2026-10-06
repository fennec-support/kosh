# The make target completion falls back to scanning the Makefile directly when no
# GNU make answers the database probe, so the bundled make still completes
# targets. PATH is emptied for the completing invocation so the probe finds no
# system make.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf 'all: build\n\ttrue\nbuild:\n\ttrue\ntest: build\n\ttrue\nclean:\n\ttrue\n.PHONY: clean\n' > "$dir/Makefile"
cd "$dir"
echo "== make targets without gnu make:"
PATH=/nonexistent "$BIN" --debug-complete-at 'make ' </dev/null

cat > "$dir/make" <<'SH'
#!/bin/sh
: > probe-ran
exec >/dev/null 2>&1
exec sleep 30
SH
chmod +x "$dir/make"
echo "== make targets after a timed out probe:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" -c \
  'koshkit timeout -k 1s 10s "$1" --debug-complete-at "make "' \
  shell "$BIN" </dev/null
printf 'completion-status=%s\n' "$?"
if [ -f probe-ran ]; then
  echo probe-started
else
  echo probe-not-started
fi
