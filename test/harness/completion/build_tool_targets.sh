# The build-tool stage lists make targets from the Makefile, the npm family's
# scripts from package.json, and cmake targets from the build directory, and
# honors -C, hermetically in a temporary directory.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
printf 'all: build test\n\nbuild:\n\ttrue\n\ntest: build\n\ttrue\n\nclean:\n\ttrue\n\n.PHONY: clean\n' > "$dir/Makefile"
printf '{ "description": "scripts", "metadata": { "scripts": { "fake": "x" } }, "scripts": { "dev": "x", "build": "y", "lint:fix": "z" } }\n' > "$dir/package.json"
cd "$dir"
echo "== make targets:"
"$BIN" --debug-complete-at 'make ' </dev/null
echo "== make by prefix:"
"$BIN" --debug-complete-at 'make cl' </dev/null
echo "== npm run scripts:"
"$BIN" --debug-complete-at 'npm run ' </dev/null
echo "== bun run by prefix:"
"$BIN" --debug-complete-at 'bun run li' </dev/null
echo "== make -C from outside:"
cd /
"$BIN" --debug-complete-at "make -C $dir bu" </dev/null

# cmake --build runs the build program that the cache selects, so the targets
# come from CMakeFiles/TargetDirectories.txt and the cmake on PATH never runs.
mkdir -p "$dir/build/CMakeFiles"
: > "$dir/build/CMakeCache.txt"
printf '%s\n' "$dir/build/CMakeFiles/app.dir" \
  "$dir/build/lib/CMakeFiles/support.dir" \
  "$dir/build/CMakeFiles/install/local.dir" \
  > "$dir/build/CMakeFiles/TargetDirectories.txt"
printf '#!/bin/sh\n: > "%s/cmake-ran"\n' "$dir" > "$dir/cmake"
chmod +x "$dir/cmake"
echo "== cmake --build targets:"
PATH="$dir${TEST_PATH_SEPARATOR}$TEST_SYSTEM_PATH" "$BIN" \
  --debug-complete-at "cmake --build $dir/build --target " </dev/null
if [ -f "$dir/cmake-ran" ]; then echo cmake-ran; else echo cmake-not-run; fi
