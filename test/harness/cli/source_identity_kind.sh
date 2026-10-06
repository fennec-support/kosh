unset KOSH_FLAGS
# A command string and a file share the spelling -c, and BASH_SOURCE names them
# differently. A function defined in a command string reports the shell name, as
# Bash does, and a function defined in a file called -c reports that file.
d=$(mktemp -d)
trap '[ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$d"' EXIT
cd "$d" || exit 1

cat > ./-c <<'EOF'
file_function() { echo "file_function: ${BASH_SOURCE[0]} ${FUNCNAME[0]}"; }
EOF

echo "== function defined in a file called -c =="
"$BIN" --mood bash -c '. -- -c; file_function' 2>&1
printf 'rc=%s\n' "$?"

echo "== function defined in a command string =="
"$BIN" --mood bash -c 'string_function() { [ "${BASH_SOURCE[0]}" = "$0" ] && echo "shell name"; }; string_function' 2>&1
printf 'rc=%s\n' "$?"

echo "== type -V names the file and the command string =="
"$BIN" --mood bash -c '. -- -c; type -V file_function | head -n 1; string_function() { :; }; type -V string_function | head -n 1' 2>&1
printf 'rc=%s\n' "$?"
