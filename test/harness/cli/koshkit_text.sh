# The koshkit text utilities run against a fixed input file in a temporary
# directory, so the counts and the sorted output are the same everywhere.
unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1
printf 'banana\napple\ncherry\napple\n' > fruit.txt
mkdir -p grep-tree/sub
printf 'hello\nnope\n' > grep-tree/a.txt
printf 'hello-again\n' > grep-tree/sub/b.txt
yes x | tr -d '\n' | head -c 65534 > grep-regex-boundary.txt
printf 'aXa\n' >> grep-regex-boundary.txt

echo "--- cat -n ---"
"$BIN" -c 'koshkit cat -n fruit.txt'
echo "--- wc ---"
"$BIN" -c 'koshkit wc fruit.txt'
echo "--- wc -l ---"
"$BIN" -c 'koshkit wc -l fruit.txt'
"$BIN" -c 'koshkit seq 20000' > batch-input.txt
: > empty.txt
printf 'first\n' > cat-first.txt
printf 'last\n' > cat-last.txt
printf 'abc' > transform-first.txt
printf '\tx\n' > transform-tabs.txt
printf '        x\n' > transform-blanks.txt
printf 'def\n' > transform-second.txt
for batch_source_index in 01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18; do
  printf 'source-%s\n' "$batch_source_index" > "batch-source-$batch_source_index.txt"
  printf '1\n' > "bc-source-$batch_source_index.txt"
done
echo "--- cat multi-chunk input with a missing operand ---"
"$BIN" -c \
  'koshkit cat batch-input.txt cat-first.txt missing.txt cat-last.txt > cat-output.txt; printf "status=%s\n" "$?"; koshkit cksum cat-output.txt' \
  2>&1
echo "--- numbered cat reads later files after a missing operand ---"
"$BIN" -c \
  'koshkit cat -n cat-first.txt missing.txt cat-last.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- numbered cat flushes large output and keeps numbering ---"
"$BIN" -c \
  'koshkit cat -n batch-input.txt missing.txt batch-input.txt 2>"$TEST_NULL_DEVICE" | koshkit cksum; koshkit cat -n batch-input.txt batch-input.txt | koshkit tail -n 1'
echo "--- cat preserves bounded source-window order ---"
"$BIN" -c \
  'koshkit cat batch-source-18.txt batch-source-01.txt batch-source-17.txt batch-source-02.txt batch-source-16.txt batch-source-03.txt batch-source-15.txt batch-source-04.txt batch-source-14.txt batch-source-05.txt batch-source-13.txt batch-source-06.txt batch-source-12.txt batch-source-07.txt batch-source-11.txt batch-source-08.txt batch-source-10.txt batch-source-09.txt'
echo "--- transformed sources preserve boundaries ---"
"$BIN" -c 'koshkit expand -t 4 transform-first.txt transform-tabs.txt'
"$BIN" -c 'koshkit unexpand transform-first.txt transform-blanks.txt' | cat -v
"$BIN" -c 'koshkit fold -w 4 transform-first.txt transform-second.txt'
echo "--- transformed utilities batch source windows ---"
"$BIN" -c 'koshkit expand batch-source-*.txt | koshkit cksum'
"$BIN" -c 'koshkit unexpand batch-source-*.txt | koshkit cksum'
"$BIN" -c 'koshkit fold batch-source-*.txt | koshkit cksum'
"$BIN" -c 'koshkit cat -n batch-source-*.txt | koshkit cksum'
echo "--- od and bc batch source windows ---"
"$BIN" -c 'koshkit od -An -tc batch-source-*.txt | koshkit cksum'
"$BIN" -c 'koshkit bc bc-source-*.txt | koshkit cksum'
echo "--- cat reads standard input between files ---"
printf 'middle\n' | "$BIN" -c \
  'koshkit cat cat-first.txt - cat-last.txt'
"$BIN" -c "koshkit yes x | koshkit tr -d '\n' | koshkit head -c 65534" \
  > strings-boundary.txt
printf 'tail\0' >> strings-boundary.txt
echo "--- strings preserves a run across chunks ---"
"$BIN" -c \
  'koshkit strings -t d strings-boundary.txt | koshkit wc -c; koshkit strings strings-boundary.txt | koshkit tail -c 5'
echo "--- strings flushes large output in source order ---"
"$BIN" -c 'koshkit strings batch-input.txt cat-first.txt | koshkit cksum'
echo "--- strings reads later files after a missing operand ---"
"$BIN" -c \
  'koshkit strings cat-first.txt missing.txt cat-last.txt; printf "status=%s\n" "$?"' \
  2>&1
"$BIN" -c "koshkit yes x | koshkit tr -d '\n' | koshkit head -c 65535" \
  > cut-boundary.txt
printf ':tail\n' >> cut-boundary.txt
echo "--- cut preserves a line across chunks ---"
"$BIN" -c 'koshkit cut -d : -f 2 cut-boundary.txt'
"$BIN" -c "koshkit yes x | koshkit tr -d '\n' | koshkit head -c 65535" \
  > cut-utf8-boundary.txt
printf '\303\251z' >> cut-utf8-boundary.txt
echo "--- cut preserves split UTF-8 at final line ---"
"$BIN" -c \
  'koshkit cut -c 65536-65537 cut-utf8-boundary.txt | koshkit od -An -tx1; koshkit cut -b 65537 -n cut-utf8-boundary.txt | koshkit od -An -tx1'
echo "--- cut reads later files after a missing operand ---"
"$BIN" -c \
  'koshkit cut -c 1-5 cat-first.txt missing.txt cat-last.txt; printf "status=%s\n" "$?"' \
  2>&1
"$BIN" -c 'koshkit seq 19999; printf "20001\n"' > diff-right.txt
echo "--- diff reads two multi-chunk files ---"
"$BIN" -c \
  'koshkit diff batch-input.txt diff-right.txt; printf "status=%s\n" "$?"'
echo "--- diff reads standard input on the right ---"
printf 'first\n' | "$BIN" -c \
  'koshkit diff cat-first.txt -; printf "status=%s\n" "$?"'
echo "--- diff reports a missing second operand ---"
"$BIN" -c \
  'koshkit diff batch-input.txt missing.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- wc multi-chunk input with a missing operand ---"
"$BIN" -c \
  'koshkit wc -c batch-input.txt missing.txt empty.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- wc counts newlines across vector blocks ---"
"$BIN" -c 'koshkit wc -l batch-input.txt cat-first.txt transform-first.txt'
"$BIN" -c 'koshkit cat batch-input.txt | koshkit wc -l'
echo "--- wc repeated explicit standard input ---"
printf 'abc\n' | "$BIN" -c 'koshkit wc -c - -'
echo "--- wc implicit standard input ---"
printf 'abc\n' | "$BIN" -c 'koshkit wc -c'
echo "--- cksum multi-chunk input with a missing operand ---"
"$BIN" -c \
  'koshkit cksum batch-input.txt missing.txt empty.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- cksum repeated explicit standard input ---"
printf 'abc\n' | "$BIN" -c 'koshkit cksum - -'
echo "--- cksum implicit standard input ---"
printf 'abc\n' | "$BIN" -c 'koshkit cksum'
echo "--- head -n 2 ---"
"$BIN" -c 'koshkit head -n 2 fruit.txt'
echo "--- tail -n 1 ---"
"$BIN" -c 'koshkit tail -n 1 fruit.txt'
echo "--- sort ---"
"$BIN" -c 'koshkit sort fruit.txt'
echo "--- sort -r ---"
"$BIN" -c 'koshkit sort -r fruit.txt'
echo "--- sort then uniq -c ---"
"$BIN" -c 'koshkit sort fruit.txt | koshkit uniq -c'
printf 'zulu\nalpha\n' > sort-a.txt
printf 'middle\nbeta\n' > sort-b.txt
echo "--- sort multiple files with a missing operand ---"
"$BIN" -c \
  'koshkit sort sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- sort repeated standard input ---"
printf 'delta\nalpha\n' | "$BIN" -c 'koshkit sort - -'
echo "--- paste multiple files with a missing operand ---"
"$BIN" -c \
  'koshkit paste -d , sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- paste repeated standard input ---"
printf 'left\nright\n' | "$BIN" -c 'koshkit paste -d , - -'
echo "--- pr merge with a missing operand ---"
"$BIN" -c \
  'koshkit pr -t -m -s , sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- pr merge suppresses a missing warning ---"
"$BIN" -c \
  'koshkit pr -r -t -m -s , sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- pr batches normal source windows ---"
"$BIN" -c 'koshkit pr -t batch-source-*.txt | koshkit cksum'
echo "--- pr numbering resets for each source ---"
"$BIN" -c 'koshkit pr -t -n cat-first.txt cat-last.txt'
echo "--- pr reads later files after a missing operand ---"
"$BIN" -c \
  'koshkit pr -t sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- pr normal mode suppresses a missing warning ---"
"$BIN" -c \
  'koshkit pr -r -t sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- grep an ---"
"$BIN" -c 'koshkit grep an fruit.txt'
echo "--- grep -v apple ---"
"$BIN" -c 'koshkit grep -v apple fruit.txt'
echo "--- grep -i APPLE ---"
"$BIN" -c 'koshkit grep -i APPLE fruit.txt'
echo "--- grep no match status ---"
"$BIN" -c 'koshkit grep absent fruit.txt; printf "status=%s\n" "$?"'
echo "--- grep invalid pattern status ---"
"$BIN" -c 'koshkit grep "[" fruit.txt; printf "status=%s\n" "$?"' 2>&1
echo "--- grep regex wildcard ---"
"$BIN" -c 'koshkit grep "a.a" fruit.txt'
echo "--- grep regex no match status ---"
"$BIN" -c 'koshkit grep "z.z" fruit.txt; printf "status=%s\n" "$?"'
echo "--- grep regex chunk boundary ---"
"$BIN" -c 'koshkit grep "a.a" grep-regex-boundary.txt > "$TEST_NULL_DEVICE"; printf "status=%s\n" "$?"'
echo "--- grep -rnh ---"
"$BIN" -c 'koshkit grep -rnh hello grep-tree'
echo "--- grep -rn ---"
"$BIN" -c 'koshkit grep -rn hello grep-tree'
echo "--- grep long recursive flags ---"
"$BIN" -c 'koshkit grep --recursive --line-number --no-filename hello grep-tree'
echo "--- grep stdin ---"
printf 'pear\nplum\n' | "$BIN" -c 'koshkit grep plum'
echo "--- grep repeated stdin ---"
printf 'pear\n' | "$BIN" -c 'koshkit grep pear - -'
echo "--- grep stream and file ordering ---"
printf 'pear\n' | "$BIN" -c 'koshkit grep pear fruit.txt -'
echo "--- grep unterminated final line ---"
printf 'tail' | "$BIN" -c 'koshkit grep tail'
printf 'pear\n' > pear.txt
echo "--- grep multiple files ---"
"$BIN" -c 'koshkit grep pear fruit.txt pear.txt'
"$BIN" -c "koshkit yes x | koshkit tr -d '\n' | koshkit head -c 65534" \
  > grep-boundary.txt
printf 'needle\n' >> grep-boundary.txt
echo "--- grep match across chunk boundary ---"
"$BIN" -c 'koshkit grep needle grep-boundary.txt | koshkit wc -c'
echo "--- grep multiple files with a missing operand ---"
"$BIN" -c \
  'koshkit grep a sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- tr to lower ---"
"$BIN" -c 'printf "AbC\n" | koshkit tr A-Z a-z'
echo "--- tr -d digits ---"
"$BIN" -c 'printf "a1b2c3\n" | koshkit tr -d 0-9'
echo "--- tr reverse range ---"
printf "abc\n" | "$BIN" -c 'koshkit tr a-c z-x'
echo "--- tr squeeze and complement ---"
printf 'aabb  cc d11\n' | "$BIN" -c 'koshkit tr -s a-z'
printf 'aabb  cc d11\n' | "$BIN" -c "koshkit tr -cs a-z '\\n'"
printf 'aabb  cc d11\n' | "$BIN" -c 'koshkit tr -c a-z _'
printf 'aabb  cc d11\n' | "$BIN" -c 'koshkit tr -cd a-z'
printf 'aabb  cc d11\n' | "$BIN" -c 'koshkit tr -ds a b'
printf 'aabb  cc d11\n' | "$BIN" -c 'koshkit tr -s ab x'
echo "--- sort keys and orderings ---"
printf 'b 2 x\na 10 y\nc 1 z\na 10 y\nB 3 w\n  d  -5\n10\n9\n-1.50\n2.5\n' \
  > sort-keys.txt
printf 'x:3:b\ny:1:a\nz:2:c\nx:3:a\n' > sort-fields.txt
for sort_args in -n -nr -u -f -b -d -i -k2 -k2n -k2,2n '-k 2n -k1' -k1.2 \
  -k1,1r -k3b -rn; do
  echo "sort $sort_args"
  "$BIN" -c "koshkit sort $sort_args sort-keys.txt"
done
for sort_args in '-t: -k2n' '-t: -k2,2n -k3' '-t: -k3 -r' '-t: -k2n -u' \
  '-t: -k1,1 -k2n'; do
  echo "sort $sort_args"
  "$BIN" -c "koshkit sort $sort_args sort-fields.txt"
done
echo "--- sort check ---"
"$BIN" -c 'koshkit sort -C sort-keys.txt; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit sort -c sort-keys.txt; printf "status=%s\n" "$?"' 2>&1
"$BIN" -c 'koshkit sort -c sort-a.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
"$BIN" -c 'koshkit sort -u -c sort-keys.txt; printf "status=%s\n" "$?"' 2>&1
echo "--- sort output file ---"
"$BIN" -c 'koshkit sort -o sort-fields.txt sort-fields.txt; koshkit cat sort-fields.txt'
echo "--- sort invalid key ---"
"$BIN" -c 'koshkit sort -k0 sort-keys.txt; printf "status=%s\n" "$?"' 2>&1
echo "--- uniq repeated unique fields and characters ---"
printf 'a 1\na 2\nb 3\nc 3\nc 3\nd\n' > uniq-input.txt
for uniq_args in -c -d -u -f1 '-f1 -c' -s2 '-s 2 -d' -cd -du; do
  echo "uniq $uniq_args"
  "$BIN" -c "koshkit uniq $uniq_args uniq-input.txt"
done
"$BIN" -c 'koshkit uniq -d uniq-input.txt uniq-output.txt; koshkit cat uniq-output.txt'
echo "--- wc characters ---"
printf 'h\303\251llo wor\n' > wc-utf8.txt
"$BIN" -c 'koshkit wc -m wc-utf8.txt'
"$BIN" -c 'koshkit wc -lmc wc-utf8.txt wc-utf8.txt'
echo "--- cat unbuffered ---"
"$BIN" -c 'koshkit cat -u wc-utf8.txt'
echo "--- od type shorthands ---"
printf 'ab\n' | "$BIN" -c 'koshkit od -b -c'
printf 'ab\n' | "$BIN" -c 'koshkit od -c -b'
printf 'ab\n' | "$BIN" -c 'koshkit od -d -o -s -x'
echo "--- xargs null items and empty input ---"
printf 'a b\0c d\0\0e' | "$BIN" -c 'koshkit xargs -0 printf "[%s]"'
echo
"$BIN" -c 'koshkit xargs -r echo ran </dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit xargs echo ran </dev/null; printf "status=%s\n" "$?"'
echo "--- env clears and removes variables ---"
"$BIN" -c 'koshkit env -i A=1 koshkit env | koshkit grep A=1'
"$BIN" -c 'export FOO=bar; koshkit env -u FOO koshkit env | koshkit grep FOO; printf "status=%s\n" "$?"'
"$BIN" -c 'export FOO=bar; koshkit env -u FOO true; printf "FOO=%s\n" "$FOO"'
echo "--- seq into head ---"
"$BIN" -c 'koshkit seq 5 | koshkit head -n 2'
echo "--- head batches regular prefix reads ---"
"$BIN" -c 'koshkit head -n 1 batch-source-*.txt | koshkit cksum'
echo "--- head bounds multi-chunk prefix reads ---"
"$BIN" -c 'koshkit head -c 5000 batch-input.txt | koshkit cksum'
"$BIN" -c 'koshkit head -n 1000 batch-input.txt | koshkit cksum'
"$BIN" -c 'koshkit cat batch-input.txt' | "$BIN" -c \
  'koshkit head -c 5000 >/dev/null; koshkit wc -c'
echo "--- head streams large output with headers in source order ---"
"$BIN" -c \
  'koshkit head -c 70000 batch-input.txt cat-first.txt batch-input.txt | koshkit cksum'
"$BIN" -c 'koshkit head -n 15000 cat-first.txt batch-input.txt | koshkit cksum'
"$BIN" -c 'koshkit cat batch-input.txt | koshkit head -n 15000 | koshkit cksum'
echo "--- head handles zero bytes without reading data ---"
"$BIN" -c 'koshkit head -c 0 cat-first.txt; printf "status=%s\n" "$?"'
echo "--- head reads later files after a missing operand ---"
"$BIN" -c \
  'koshkit head -n 1 cat-first.txt missing.txt cat-last.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- head preserves a standard-input source barrier ---"
printf 'middle\nextra\n' | "$BIN" -c \
  'koshkit head -n 1 cat-first.txt - cat-last.txt'
echo "--- head minimum signed drop count ---"
printf 'one\ntwo\n' | "$BIN" -c 'koshkit head -n -9223372036854775808'
echo "status=$?"
echo "--- tee then read back ---"
"$BIN" -c 'koshkit seq 2 | koshkit tee tee.txt'
"$BIN" -c 'koshkit cat tee.txt'
echo "--- tee copies to multiple files and appends ---"
printf 'old\n' > tee-first.txt
printf 'one\ntwo\n' | "$BIN" -c \
  'koshkit tee -a tee-first.txt tee-second.txt'
echo "first:"
"$BIN" -c 'koshkit cat tee-first.txt'
echo "second:"
"$BIN" -c 'koshkit cat tee-second.txt'
echo "--- empty tee input truncates outputs ---"
printf 'old\n' > tee-empty.txt
printf '' | "$BIN" -c 'koshkit tee tee-empty.txt'
"$BIN" -c 'koshkit wc -c < tee-empty.txt'
echo "--- tee copies more than one chunk ---"
"$BIN" -c \
  'koshkit seq 20000 | koshkit tee tee-large-1.txt tee-large-2.txt tee-large-3.txt tee-large-4.txt tee-large-5.txt tee-large-6.txt tee-large-7.txt tee-large-8.txt | koshkit wc -l'
"$BIN" -c 'koshkit cmp tee-large-1.txt tee-large-8.txt'
echo "same=$?"
echo "--- tee keeps working outputs after an open failure ---"
mkdir tee-directory
printf 'kept\n' | "$BIN" -c \
  'koshkit tee tee-working.txt tee-directory 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit cat tee-working.txt'
echo "--- seq with step ---"
"$BIN" -c 'koshkit seq 2 2 8'
printf 'alpha foo beta\nplain\nFOO and foo\n' > color-input.txt
printf 'a\nb\nc\n' > color-left.txt
printf 'a\nB\nc\nd\n' > color-right.txt
echo "--- grep color always marks matches, names, numbers, and separators ---"
"$BIN" -c 'koshkit grep --color=always -n foo color-input.txt' | tr '\033' '@'
"$BIN" -c 'koshkit grep --color=always -i foo color-input.txt grep-tree/a.txt' |
  tr '\033' '@'
echo "--- grep color highlights every alternative ---"
"$BIN" -c 'koshkit grep --color=always -E "o+|ph" color-input.txt' |
  tr '\033' '@'
echo "--- grep color leaves selected lines of -v and -x plain or whole ---"
"$BIN" -c 'koshkit grep --color=always -v foo color-input.txt' | tr '\033' '@'
"$BIN" -c 'koshkit grep --color=always -x plain color-input.txt' |
  tr '\033' '@'
echo "--- grep color colors -c and -l names but not counts ---"
"$BIN" -c 'koshkit grep --color=always -c foo color-input.txt grep-tree/a.txt' |
  tr '\033' '@'
"$BIN" -c 'koshkit grep --color=always -l foo color-input.txt' | tr '\033' '@'
echo "--- grep color never, auto, and redirected default keep bytes ---"
"$BIN" -c 'koshkit grep --color=never -n foo color-input.txt' | tr '\033' '@'
"$BIN" -c 'koshkit grep --color -n foo color-input.txt' | tr '\033' '@'
"$BIN" -c 'koshkit grep -n foo color-input.txt' | tr '\033' '@'
"$BIN" -c 'koshkit grep --color=sometimes foo color-input.txt' 2>&1 |
  tr '\033' '@'
echo "--- diff color marks removed, added, hunk, and header lines ---"
"$BIN" -c 'koshkit diff --color=always color-left.txt color-right.txt' |
  tr '\033' '@'
"$BIN" -c \
  'koshkit diff --color=always -u -L left -L right color-left.txt color-right.txt' |
  tr '\033' '@'
echo "--- diff color never and redirected default keep bytes ---"
"$BIN" -c 'koshkit diff --color=never color-left.txt color-right.txt' |
  tr '\033' '@'
"$BIN" -c 'koshkit diff -u -L left -L right color-left.txt color-right.txt' |
  tr '\033' '@'
echo "--- df human readable header and unit shape ---"
"$BIN" -c 'koshkit df -h . | koshkit head -n 1'
"$BIN" -c 'koshkit df -H . | koshkit head -n 1'
"$BIN" -c 'koshkit df -h . | koshkit tail -n 1' |
  grep -Ev '^[^ ]+ [0-9.]+[KMGTP]? [0-9.]+[KMGTP]? [0-9.]+[KMGTP]? [0-9]+% .+$'
echo "malformed-rows=$?"
