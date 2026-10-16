# For a known utility, an empty argument suggests only the file types it opens
# plus directories, a hard filter that keeps the listing short. A typed prefix
# keeps every match and hides nothing. Directories come first, and the matching
# extension floats ahead of the other files. A program with no table entry is
# unaffected. The document and media viewers limit an empty argument the same
# way: a pdf viewer shows a .pdf, an image viewer an image.
dir=$(mktemp -d)
trap 'cd / && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
: > "$dir/archive.zip"
: > "$dir/OLD.ZIP"
: > "$dir/backup.tar.gz"
: > "$dir/notes.txt"
: > "$dir/data.csv"
: > "$dir/book.pdf"
: > "$dir/photo.png"
mkdir "$dir/sub"
export PATH="$dir"
cd "$dir"
echo "== unzip with an empty argument shows only .zip files and directories:"
"$BIN" --debug-complete-at 'unzip ' </dev/null
echo "== tar with an empty argument shows only its archive types:"
"$BIN" --debug-complete-at 'tar ' </dev/null
echo "== a program with no table entry lists directories, then files in alphabetical order:"
"$BIN" --debug-complete-at 'cat ' </dev/null
echo "== mupdf with an empty argument shows only the pdf and directories:"
"$BIN" --debug-complete-at 'mupdf ' </dev/null
echo "== evince with an empty argument shows only the pdf and directories:"
"$BIN" --debug-complete-at 'evince ' </dev/null
echo "== feh with an empty argument shows only the image and directories:"
"$BIN" --debug-complete-at 'feh ' </dev/null
echo "== a typed prefix keeps a non-matching file too:"
"$BIN" --debug-complete-at 'mupdf note' </dev/null
