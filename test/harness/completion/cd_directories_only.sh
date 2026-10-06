# The cd builtin completes only directories, never files, while another command
# still completes both. Every directory candidate ends in a slash, including
# dot-dot and the directories cd reaches through CDPATH. A hermetic temp
# directory keeps the candidates stable.
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
mkdir "$dir/alpha_dir" "$dir/beta_dir"
: > "$dir/alpha_file"
: > "$dir/beta_file"
cd "$dir"
echo "== cd offers only directories:"
"$BIN" --debug-complete-at 'cd alpha' </dev/null
echo "== cd with no prefix lists the directories:"
"$BIN" --debug-complete-at 'cd ' </dev/null
echo "== ls still offers files and directories:"
"$BIN" --debug-complete-at 'ls alpha' </dev/null
echo "== dot and dot-dot complete with a slash:"
"$BIN" --debug-complete-at 'cd ..' </dev/null
"$BIN" --debug-complete-at 'cd alpha_dir/..' </dev/null
echo "== a CDPATH directory completes with a slash:"
mkdir -p "$dir/elsewhere/gamma_dir"
CDPATH="$dir/elsewhere" "$BIN" --debug-complete-at 'cd gam' </dev/null
echo "== a dot-led operand skips CDPATH:"
CDPATH="$dir/elsewhere" "$BIN" --debug-complete-at 'cd ./gam' </dev/null
