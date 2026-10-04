# Tail keeps exact POSIX count semantics while batching regular-file metadata
# and bounded positioned reads. The source order remains the operand order.
unset KOSH_FLAGS
d=$(mktemp -d)
normalized_d=$(printf '%s\n' "$d" | tr '\\' '/')
printf 'a\nb\nc\n' > "$d/with-final.txt"
printf 'a\nb\nc' > "$d/no-final.txt"
: > "$d/empty.txt"
printf '0123456789' > "$d/bytes.txt"
printf 'one\ntwo\nthree\nfour\n' > "$d/forward-lines.txt"
awk 'BEGIN { for (i = 1; i <= 70000; i++) print "x" }' \
  > "$d/large-forward.txt"
for batch_source_index in 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18; do
  printf 'source-%s\nfirst-%s\nlast-%s\n' \
    "$batch_source_index" "$batch_source_index" "$batch_source_index" \
    > "$d/batch-$batch_source_index.txt"
done

echo "--- line and byte boundaries ---"
echo "tail -n 2 without final newline:"
"$BIN" -c "koshkit tail -n 2 '$d/no-final.txt'"
printf '\n'
echo "tail -n 0 empty file:"
"$BIN" -c "koshkit tail -n 0 '$d/empty.txt'"
printf '\n'
echo "tail -c 4:"
"$BIN" -c "koshkit tail -c 4 '$d/bytes.txt'"
printf '\n'
echo "--- positive offsets and standard input ---"
echo "tail -n +2:"
"$BIN" -c "koshkit tail -n +2 '$d/with-final.txt'"
printf '\n'
echo "tail -n +2 preserves the complete positioned suffix:"
"$BIN" -c "koshkit tail -n +2 '$d/forward-lines.txt'"
printf '\n'
echo "tail -c +4:"
"$BIN" -c "koshkit tail -c +4 '$d/bytes.txt'"
printf '\n'
echo "tail from standard input:"
printf 'a\nb\nc\n' | "$BIN" -c 'koshkit tail -n 2'
printf '\n'
echo "--- forward batch boundary ---"
echo "tail -n +65538 line count:"
"$BIN" -c "koshkit tail -n +65538 '$d/large-forward.txt' | koshkit wc -l"
echo "tail -c +65537 byte count:"
"$BIN" -c "koshkit tail -c +65537 '$d/large-forward.txt' | koshkit wc -c"
printf '\n'
echo "--- bounded source order ---"
"$BIN" -c "koshkit tail -n 1 '$d/batch-01.txt' '$d/batch-02.txt' '$d/batch-03.txt' '$d/batch-04.txt' '$d/batch-05.txt' '$d/batch-06.txt' '$d/batch-07.txt' '$d/batch-08.txt' '$d/batch-09.txt' '$d/batch-10.txt' '$d/batch-11.txt' '$d/batch-12.txt' '$d/batch-13.txt' '$d/batch-14.txt' '$d/batch-15.txt' '$d/batch-16.txt' '$d/batch-17.txt' '$d/batch-18.txt'" \
  | tr '\\' '/' | sed "s#$normalized_d#TMPDIR#g"
echo "--- streamed positioned output keeps headers and order ---"
"$BIN" -c "cd '$d' && koshkit tail -n 69999 large-forward.txt with-final.txt | koshkit cksum"
"$BIN" -c "cd '$d' && koshkit tail -c 70000 large-forward.txt bytes.txt | koshkit cksum"
"$BIN" -c "cd '$d' && koshkit tail -n +2 large-forward.txt forward-lines.txt | koshkit cksum"
printf 'p\nq\n' | "$BIN" -c \
  "cd '$d' && koshkit tail -n 1 with-final.txt - no-final.txt"
printf '\n'
echo "--- missing source preserves later output and status ---"
{
  cd "$d" || exit 1
  "$BIN" -c 'koshkit tail -n 1 missing.txt with-final.txt'
  printf 'status=%s\n' "$?"
} 2>&1 | tr '\\' '/' | sed "s#$normalized_d#TMPDIR#g"

(
cd "$d" || exit 1
echo "--- counts accept only plain decimal digits ---"
for count_spec in 99999999999999999999 '1 ' ++1 +99999999999999999999 ''; do
  for count_command in "tail -n" "tail -c" "head -n" "head -c"; do
    count_output=$("$BIN" -c "koshkit $count_command \"\$1\" \"\$2\"" count \
      "$count_spec" with-final.txt 2>&1)
    count_status=$?
    printf '%s [%s] status=%s\n' "$count_command" \
      "$(printf '%s\n' "$count_output" | sed 1q)" "$count_status"
  done
done
"$BIN" -c 'koshkit tail -n " 1" with-final.txt; koshkit head -n +1 with-final.txt; koshkit tail -n -+1 with-final.txt'
echo "--- counts up to the largest unsigned value select everything ---"
for huge_count in 9223372036854775808 18446744073709551615; do
  "$BIN" -c "koshkit tail -n $huge_count bytes.txt with-final.txt; koshkit tail -c $huge_count bytes.txt; koshkit head -n $huge_count with-final.txt; koshkit head -c $huge_count bytes.txt"
  printf '\n'
  printf 'p\nq\n' | "$BIN" -c "koshkit tail -n $huge_count"
  "$BIN" -c "koshkit tail -n +$huge_count with-final.txt; koshkit tail -c +$huge_count bytes.txt; echo \"status=\$?\""
done
echo "--- standard input and empty counts in headers ---"
printf 'p\nq\n' | "$BIN" -c 'koshkit head -n 1 - with-final.txt'
printf 'p\nq\n' | "$BIN" -c 'koshkit tail -n 1 - with-final.txt'
echo "tail -n 0 writes no header:"
"$BIN" -c 'koshkit tail -n 0 with-final.txt empty.txt missing.txt; echo "status=$?"'
echo "tail -c 0 writes no header:"
"$BIN" -c 'koshkit tail -c 0 with-final.txt empty.txt; echo "status=$?"'
echo "tail -n +0 keeps every header:"
"$BIN" -c 'koshkit tail -n +0 bytes.txt empty.txt'
echo "a directory operand keeps its header and error:"
mkdir dir
normalize_directory_error()
{
  sed -e "s/cannot open 'dir'/cannot read 'dir'/" \
    -e 's/Access is denied\./Is a directory./'
}
"$BIN" -c 'koshkit tail -n 1 dir with-final.txt; echo "status=$?"' 2>&1 |
  normalize_directory_error
"$BIN" -c 'koshkit head -n 1 dir with-final.txt; echo "status=$?"' 2>&1 |
  normalize_directory_error
"$BIN" -c 'koshkit head -n -1 dir with-final.txt; echo "status=$?"' 2>&1 |
  normalize_directory_error
echo "--- page sized files are read to the end ---"
head -c 4096 large-forward.txt > page.txt
"$BIN" -c 'koshkit tail -c 5 page.txt | koshkit wc -c; koshkit wc -c page.txt; koshkit head -c -5 page.txt | koshkit wc -c; koshkit tail -n 1 page.txt'
head -c 8192 large-forward.txt > two-pages.txt
"$BIN" -c 'koshkit tail -c 5 two-pages.txt | koshkit wc -c; koshkit wc -c two-pages.txt; koshkit head -c -5 two-pages.txt | koshkit wc -c; koshkit tail -c +8190 two-pages.txt | koshkit wc -c'
echo "--- negative head counts across the 64 KiB read boundary ---"
for negative_count in 65535 65536 65537; do
  for negative_mode in n c; do
    expected_sum=$(head -$negative_mode -$negative_count large-forward.txt | cksum)
    actual_sum=$("$BIN" -c "koshkit head -$negative_mode -$negative_count large-forward.txt | koshkit cksum")
    if [ "$actual_sum" = "$expected_sum" ]; then
      echo "head -$negative_mode -$negative_count: matched"
    else
      echo "head -$negative_mode -$negative_count: wrong [$actual_sum] [$expected_sum]"
    fi
  done
done
echo "head -n -1 without a final newline:"
"$BIN" -c 'koshkit head -n -1 no-final.txt'
printf '|\n'
expected_sum=$(head -n -1 large-forward.txt no-final.txt with-final.txt | cksum)
actual_sum=$("$BIN" -c 'koshkit head -n -1 large-forward.txt no-final.txt with-final.txt | koshkit cksum')
if [ "$actual_sum" = "$expected_sum" ]; then
  echo "head -n -1 multiple sources: matched"
else
  echo "head -n -1 multiple sources: wrong [$actual_sum] [$expected_sum]"
fi
expected_sum=$(head -c -65536 large-forward.txt bytes.txt large-forward.txt | cksum)
actual_sum=$("$BIN" -c 'koshkit head -c -65536 large-forward.txt bytes.txt large-forward.txt | koshkit cksum')
if [ "$actual_sum" = "$expected_sum" ]; then
  echo "head -c -65536 multiple sources: matched"
else
  echo "head -c -65536 multiple sources: wrong [$actual_sum] [$expected_sum]"
fi
)
if [ "${TARGET:-$(uname -s)}" = Linux ]; then
  for pseudo_file in /proc/version /sys/kernel/mm/transparent_hugepage/enabled; do
    [ -r "$pseudo_file" ] || continue

    pseudo_text=$(cat "$pseudo_file")
    pseudo_size=$(cat "$pseudo_file" | wc -c)
    pseudo_size=$((pseudo_size + 0))
    pseudo_last=$(printf '%s\n' "$pseudo_text" | tail -n 1)
    [ "$("$BIN" -c 'koshkit tail -n 1 "$1"' x "$pseudo_file")" = "$pseudo_last" ] ||
      echo "pseudo-tail-n=wrong $pseudo_file"
    [ "$("$BIN" -c 'koshkit tail -c +3 "$1"' x "$pseudo_file")" = "$(printf '%s\n' "$pseudo_text" | tail -c +3)" ] ||
      echo "pseudo-tail-offset=wrong $pseudo_file"
    [ "$("$BIN" -c 'koshkit tail -c 5 "$1"' x "$pseudo_file")" = "$(printf '%s\n' "$pseudo_text" | tail -c 5)" ] ||
      echo "pseudo-tail-c=wrong $pseudo_file"
    [ "$("$BIN" -c 'koshkit head -c -5 "$1"' x "$pseudo_file" | cksum)" = "$(printf '%s\n' "$pseudo_text" | head -c -5 | cksum)" ] ||
      echo "pseudo-head-c=wrong $pseudo_file"
    [ "$("$BIN" -c 'koshkit head -n -0 "$1"' x "$pseudo_file" | cksum)" = "$(printf '%s\n' "$pseudo_text" | cksum)" ] ||
      echo "pseudo-head-n=wrong $pseudo_file"
    [ "$("$BIN" -c 'koshkit wc -c "$1"' x "$pseudo_file")" = "$pseudo_size $pseudo_file" ] ||
      echo "pseudo-wc=wrong $pseudo_file"
  done
fi
echo "--- wc words start at a printable byte ---"
printf '\314\307\200\324' | "$BIN" -c 'LC_ALL=C; export LC_ALL; koshkit wc -w'
printf '\033\ra' | "$BIN" -c 'koshkit wc -w'
printf 'a\033b \033\n\200 c\177d\n' | "$BIN" -c 'koshkit wc -lw'
printf 'ab\033' | "$BIN" -c 'koshkit wc -w'

if [ -n "$d" ]; then
  "$BIN_DIR/invoke-koshkit" rm -r "$d"
fi
