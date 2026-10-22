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
echo "--- head -2 and tail -1 take the historical count form ---"
"$BIN" -c 'koshkit head -2 fruit.txt; koshkit tail -1 fruit.txt'
echo "--- a -2 after a file is a file operand ---"
"$BIN" -c 'koshkit head fruit.txt -2 2>/dev/null; printf "status=%s\n" "$?"'
echo "--- sort -r ---"
"$BIN" -c 'koshkit sort -r fruit.txt'
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
  'koshkit pr -t -m -s, sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
  2>&1
echo "--- pr merge suppresses a missing warning ---"
"$BIN" -c \
  'koshkit pr -r -t -m -s, sort-a.txt missing.txt sort-b.txt; printf "status=%s\n" "$?"' \
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
echo "--- grep invalid pattern status ---"
"$BIN" -c 'koshkit grep "[" fruit.txt; printf "status=%s\n" "$?"' 2>&1
echo "--- grep regex chunk boundary ---"
"$BIN" -c 'koshkit grep "a.a" grep-regex-boundary.txt > "$TEST_NULL_DEVICE"; printf "status=%s\n" "$?"'
echo "--- grep -rnh ---"
"$BIN" -c 'koshkit grep -rnh hello grep-tree'
echo "--- grep -rn ---"
"$BIN" -c 'koshkit grep -rn hello grep-tree'
echo "--- grep long recursive flags ---"
"$BIN" -c 'koshkit grep --recursive --line-number --no-filename hello grep-tree'
echo "--- grep one file system changes nothing on one file system ---"
"$BIN" -c 'koshkit grep -rn --one-file-system hello grep-tree'
echo "--- grep -r names the file of a one-file tree ---"
"$BIN" -c 'koshkit grep -rn again grep-tree/sub'
echo "--- grep -r with no operand searches the working directory ---"
(cd grep-tree/sub && "$BIN" -c 'koshkit grep -r again' < /dev/null)
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
echo "--- tr truncation, repeats, and class checks ---"
printf 'Hello World 123 abc\n' > tr-input.txt
"$BIN" -c '
koshkit tr -t abcdef xy < tr-input.txt
koshkit tr -t "[:lower:]" AB < tr-input.txt
koshkit tr "[:lower:][:upper:]" "[:upper:][:lower:]" < tr-input.txt
koshkit tr "[a*3]bc" xyz < tr-input.txt
koshkit tr -c "[:lower:]\n" "[x*]" < tr-input.txt
koshkit tr "[=l=]\101-\103" "x" < tr-input.txt
koshkit tr "a-z" "[:upper:]" < tr-input.txt
echo "status=$?"
koshkit tr "a" "[:digit:]" < tr-input.txt
echo "status=$?"
koshkit tr "[:upper:]0-9" "[:lower:]" < tr-input.txt
echo "status=$?"
koshkit tr "-c" "[:lower:]" "xy" < tr-input.txt
echo "status=$?"
koshkit tr "[:foo:]" "x" < tr-input.txt
echo "status=$?"
koshkit tr "[a*]" "x" < tr-input.txt
echo "status=$?"
koshkit tr "a" "[b*x]" < tr-input.txt
echo "status=$?"
koshkit tr "a" "[=x=]" < tr-input.txt
echo "status=$?"
koshkit tr "-ds" "a" "[b*]" < tr-input.txt
echo "status=$?"
koshkit tr "-t" "abc" "[:upper:]" < tr-input.txt
echo "status=$?"
koshkit tr a "" < tr-input.txt
echo "status=$?"
' 2>/dev/null
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
echo "--- sort general, human, month, and version orderings ---"
printf '1.5e3\n2\n-1e-2\nabc\n\n  3.0\ninf\n-inf\nnan\n+4\n1E2\n.5\n' \
  > sort-general.txt
printf 'b 2K\na 1M\nc 1k\nd 1.5K\ne 500\nf -2G\ng 0K\nh 2K\n' > sort-human.txt
printf 'Jan 1\njanuary\nfoo\nDEC\n  feb\nma\nsEp x\nAug\n' > sort-month.txt
printf 'a1.10\na1.2\n1.0~rc1\n1.0\n1.0a\n.\n..\n.hidden\nf-1.10.tar.gz\n' \
  > sort-version.txt
printf 'f-1.2.tar.gz\nB2\nb10\nv001\nv1\n' >> sort-version.txt
for sort_args in -g -gr -gu; do
  echo "sort $sort_args"
  "$BIN" -c "koshkit sort $sort_args sort-general.txt"
done
for sort_args in -h -k2h -k2hr '-k2,2h -u' '-k2,2h -k1r'; do
  echo "sort $sort_args"
  "$BIN" -c "koshkit sort $sort_args sort-human.txt"
done
for sort_args in -M -Mr -k1,1M; do
  echo "sort $sort_args"
  "$BIN" -c "koshkit sort $sort_args sort-month.txt"
done
for sort_args in -V -Vr -fV -Vd; do
  echo "sort $sort_args"
  "$BIN" -c "koshkit sort $sort_args sort-version.txt"
done
"$BIN" -c 'koshkit sort -V sort-version.txt | koshkit sort -CV; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit sort -CV sort-version.txt; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit sort -gn sort-general.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit sort -k1Vi sort-version.txt >/dev/null; printf "status=%s\n" "$?"'
echo "--- sort NUL-separated lines ---"
"$BIN" -c "printf 'b\\0a\\0c\\nx\\0a\\0' | koshkit sort -zu | koshkit od -c"
"$BIN" -c "printf 'b\\0a\\0c\\nx' | koshkit sort -zr | koshkit od -c"
echo "--- uniq repeated unique fields and characters ---"
printf 'a 1\na 2\nb 3\nc 3\nc 3\nd\n' > uniq-input.txt
for uniq_args in -c -d -u -f1 '-f1 -c' -s2 '-s 2 -d' -cd -du; do
  echo "uniq $uniq_args"
  "$BIN" -c "koshkit uniq $uniq_args uniq-input.txt"
done
"$BIN" -c 'koshkit uniq -d uniq-input.txt uniq-output.txt; koshkit cat uniq-output.txt'
echo "--- uniq case, check length, all repeated, and groups ---"
printf 'a 1\na 2\nb 3\nc 3\nc 3\nApple\napple\nAPPLE x\nfoo1\nfoo2\nfoo2\n' \
  > uniq-groups.txt
for uniq_args in -i -ic -iu -w1 '-w 3 -c' -D -Du '-D -w1' \
  --all-repeated=separate --all-repeated=p --group --group=prepend \
  --group=append '--group=both -i' '-f1 -w1 -D'; do
  echo "uniq $uniq_args"
  "$BIN" -c "koshkit uniq $uniq_args uniq-groups.txt"
done
"$BIN" -c 'koshkit uniq --group -c uniq-groups.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit uniq -Dc uniq-groups.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit uniq -w x uniq-groups.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c "printf 'a\\0A\\0b\\nx\\0b\\ny' | koshkit uniq -zic | koshkit od -c"
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
printf 'abcdefghijklmnopqrs' | "$BIN" -c 'koshkit od -t u2 -t c'
printf 'ab\n' | "$BIN" -c 'koshkit od -A n -t x1 -t c'
"$BIN" -c 'koshkit yes | koshkit od -c -N 20'
echo "--- xargs null items and empty input ---"
printf 'a b\0c d\0\0e' | "$BIN" -c 'koshkit xargs -0 printf "[%s]"'
echo
"$BIN" -c 'koshkit xargs -r echo ran </dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit xargs echo ran </dev/null; printf "status=%s\n" "$?"'
echo "--- xargs batches, replacement, and statuses ---"
printf 'a b c d e\n' | "$BIN" -c 'koshkit xargs -n 2 echo'
printf 'a\nb\nc\n' | "$BIN" -c 'koshkit xargs -L 2 echo'
printf '  x y \nz\n' | "$BIN" -c "koshkit xargs -I{} echo '[{}]'"
printf 'a stop b\n' | "$BIN" -c 'koshkit xargs -E stop echo'
printf 'aaa bbb ccc\n' | "$BIN" -c 'koshkit xargs -s 12 echo'
printf 'x\n' | "$BIN" -c 'koshkit xargs false; printf "fails=%s\n" "$?"'
echo "--- xargs runs each batch before the input ends ---"
"$BIN" -c 'koshkit yes | koshkit xargs echo | koshkit head -c 10'
echo
echo "--- env clears and removes variables ---"
"$BIN" -c 'koshkit env -i A=1 koshkit env | koshkit grep A=1'
"$BIN" -c 'export FOO=bar; koshkit env -u FOO koshkit env | koshkit grep FOO; printf "status=%s\n" "$?"'
"$BIN" -c 'export FOO=bar; koshkit env -u FOO true; printf "FOO=%s\n" "$FOO"'
"$BIN" -c 'koshkit env -i env; printf "empty-environment-status=%s\n" "$?"'
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
echo "--- seq with fractions, widths, and formats ---"
"$BIN" -c 'koshkit seq -s, 1 0.5 3; koshkit seq 0.1 0.1 0.4; koshkit seq -w -1 0.5 1; koshkit seq 1.5e1 17; koshkit seq -f "a%5.2fb" 1 2; koshkit seq -s "" 3; koshkit seq -- -0 1; koshkit seq 0 0.000001 0.000003; koshkit seq 3 1; koshkit seq 0x10 0x11'
"$BIN" -c 'koshkit seq -f %d 1; echo "status=$?"; koshkit seq -f "%g%" 1; echo "status=$?"; koshkit seq 1 0 x; echo "status=$?"; koshkit seq nan; echo "status=$?"' 2>/dev/null
printf 'alpha foo beta\nplain\nFOO and foo\n' > color-input.txt
printf 'a\nb\nc\n' > color-left.txt
printf 'a\nB\nc\nd\n' > color-right.txt
echo "--- grep color modes keep plain output when redirected ---"
"$BIN" -c 'koshkit grep --color=never -n foo color-input.txt'
"$BIN" -c 'koshkit grep --color -n foo color-input.txt'
"$BIN" -c 'koshkit grep -n foo color-input.txt'
"$BIN" -c 'koshkit grep --color=sometimes foo color-input.txt' 2>&1
echo "--- diff color modes keep plain output when redirected ---"
"$BIN" -c 'koshkit diff --color=never color-left.txt color-right.txt'
"$BIN" -c 'koshkit diff -u -L left -L right color-left.txt color-right.txt'
echo "--- df human readable header and aligned rows ---"
"$BIN" -c 'koshkit df -h . | koshkit head -n 1'
"$BIN" -c 'koshkit df -H . | koshkit head -n 1'
"$BIN" -c 'koshkit df -h . | koshkit tail -n 1' |
  grep -Ev '^[^ ]+ +[0-9.]+[KMGTP]? +[0-9.]+[KMGTP]? +[0-9.]+[KMGTP]? +[0-9]+% .+$'
echo "malformed-rows=$?"
"$BIN" -c 'koshkit df -H . | koshkit tail -n 1' |
  grep -Ev '^[^ ]+ +[0-9.]+[kMGTP]? +[0-9.]+[kMGTP]? +[0-9.]+[kMGTP]? +[0-9]+% .+$'
echo "malformed-si-rows=$?"
"$BIN" -c 'koshkit df -H . | koshkit tail -n 1' | grep -Eq '[0-9]K '
echo "uppercase-si-kilo=$?"
"$BIN" -c 'koshkit df -h . /' |
  awk 'NR == 1 { start = index($0, "Mounted on") }
       NR > 1 { if (index($0, "% ") + 2 != start) misaligned = 1 }
       END { print "df-columns-aligned=" (misaligned ? 0 : 1) }'
echo "--- chmod accepts leading zeros up to 7777 ---"
: > chmod-zeros.txt
"$BIN" -c 'koshkit chmod 00640 chmod-zeros.txt && koshkit stat -c %a chmod-zeros.txt'
"$BIN" -c 'koshkit chmod 017777 chmod-zeros.txt 2>/dev/null; printf "status=%s\n" "$?"'
echo "--- stat prints times before 1970 ---"
TZ=UTC0 "$BIN" -c 'koshkit touch -t 196001010000 old-stamp && koshkit stat -c %Y old-stamp'
echo "--- paste and expand stream an endless input ---"
"$BIN" -c 'koshkit yes | koshkit paste - - | koshkit head -n 2'
"$BIN" -c 'koshkit yes "$(printf "a\tb")" | koshkit expand | koshkit head -n 1'
echo "--- expand and unexpand count a carriage return as a column ---"
printf 'ab\r\tc\n' | "$BIN" -c 'koshkit expand' | tr '\r' R
printf 'ab\r        c\n' | "$BIN" -c 'koshkit unexpand -a' | tr '\r\t' RT
echo "--- pr pages carry no separator lines ---"
"$BIN" -c 'koshkit seq 7 | koshkit pr -t -l 3' | tr '\n' ' '
echo
"$BIN" -c 'koshkit seq 3 | koshkit pr -F -l 12 -D D -h T' | tr '\f\n' 'F|'
echo
"$BIN" -c 'koshkit seq 30 | koshkit pr +3 -d -l 16 -D D -h T' | tr '\n' '|'
echo
"$BIN" -c 'koshkit seq 3 | koshkit pr -d -l 13 -D D -h T' | tr '\n' '|'
echo
echo "--- pr lays out equal columns as GNU does ---"
printf 'alpha\nbe\tta\ngamma line long enough to be cut at the column edge\n\tlead\n' \
  > pr-columns.txt
"$BIN" -c 'koshkit pr -t -2 pr-columns.txt' | tr '\t' T
"$BIN" -c 'koshkit pr -t -3 -w 30 pr-columns.txt' | tr '\t' T
"$BIN" -c 'koshkit seq 7 | koshkit pr -t -3 -n' | tr '\t' T
"$BIN" -c 'koshkit seq 5 | koshkit pr -t -a -2 -o 3' | tr '\t' T
"$BIN" -c 'koshkit seq 7 | koshkit pr -t -d -2 -l 4' | tr '\t\n' 'T|'
echo
"$BIN" -c 'koshkit seq 6 | koshkit pr -t -3 -s,'
"$BIN" -c 'koshkit seq 6 | koshkit pr -t -3 -s'
"$BIN" -c 'koshkit seq 4 | koshkit pr -2 -l 12 -D D -h T' | tr '\t\n' 'T|'
echo
echo "--- pr merges files with a separator and line numbers ---"
printf 'a\nb\nc\n' > pr-merge-long.txt
printf 'x\n' > pr-merge-short.txt
"$BIN" -c 'koshkit pr -m -t -s, pr-merge-short.txt pr-merge-long.txt'
"$BIN" -c 'koshkit pr -m -t -n pr-merge-short.txt pr-merge-long.txt' | tr '\t' T
"$BIN" -c 'koshkit pr -m -t -w 30 -s, pr-merge-long.txt pr-merge-short.txt' | tr '\t' T
echo "--- pr selects a page range ---"
"$BIN" -c 'koshkit seq 9 | koshkit pr -t -l 2 +2:3 --columns=2' | tr '\t' T
echo "--- od skips with a zero count ---"
printf 'abcdefghij' | "$BIN" -c 'koshkit od -N0 -j5; printf "status=%s\n" "$?"'
echo "--- realpath follows a dangling link and rejects a file with a slash ---"
mkdir -p realpath-links && : > realpath-links/file
ln -s nowhere realpath-links/dangling
"$BIN" -c 'resolved=$(koshkit realpath realpath-links/dangling) && koshkit basename "$resolved"'
"$BIN" -c 'koshkit realpath realpath-links/file/ 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c "koshkit realpath '' 2>/dev/null; printf 'status=%s\n' \"\$?\""
echo "--- split widens its suffixes past yz ---"
mkdir split-widen
(cd split-widen && "$BIN" -c 'koshkit seq 652 | koshkit split -l 1' && ls | tail -n 3)
echo "--- comm reports unsorted input ---"
printf 'b\na\n' > comm-unsorted-1.txt
printf 'a\nb\n' > comm-unsorted-2.txt
"$BIN" -c 'koshkit comm comm-unsorted-1.txt comm-unsorted-2.txt 2>/dev/null; printf "status=%s\n" "$?"'
echo "--- comm order checks, delimiters, totals, and NUL lines ---"
printf 'a\nb\nd\ne\n' > comm-left.txt
printf 'b\nc\ne\nf\n' > comm-right.txt
printf 'b\na\n' > comm-paired.txt
for comm_args in --total '--total -12' --output-delimiter=:: \
  '--output-delimiter=+ --total -2'; do
  echo "comm $comm_args"
  "$BIN" -c "koshkit comm $comm_args comm-left.txt comm-right.txt"
done
"$BIN" -c "koshkit comm --output-delimiter '' comm-left.txt comm-right.txt | koshkit od -c"
"$BIN" -c 'koshkit comm --output-delimiter=: --output-delimiter=, comm-left.txt comm-right.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit comm comm-paired.txt comm-paired.txt; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit comm --check-order comm-paired.txt comm-paired.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit comm --check-order --total comm-unsorted-1.txt comm-unsorted-2.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit comm --total comm-unsorted-1.txt comm-unsorted-2.txt 2>/dev/null; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit comm --check-order --nocheck-order comm-unsorted-1.txt comm-unsorted-2.txt; printf "status=%s\n" "$?"'
"$BIN" -c "printf 'a\\0b\\nx\\0d' > comm-zero-1.txt; printf 'b\\nx\\0c\\0d\\0' > comm-zero-2.txt"
"$BIN" -c 'koshkit comm -z --total comm-zero-1.txt comm-zero-2.txt | koshkit od -c'
echo "--- wc pads every count to one width ---"
printf 'hello world\nfoo bar baz\n' > wc-width.txt
seq 1 100000 > wc-width-big.txt
"$BIN" -c 'koshkit wc -lw wc-width.txt; koshkit wc -l wc-width.txt; koshkit wc -l wc-width.txt wc-width.txt'
"$BIN" -c 'koshkit wc -l wc-width.txt wc-width-big.txt; koshkit wc -c wc-width.txt wc-width-big.txt'
"$BIN" -c 'koshkit wc < wc-width.txt; koshkit wc -lw < wc-width.txt; koshkit wc -l < wc-width.txt'
"$BIN" -c 'printf "a b\nc\n" | koshkit wc; printf "a b\nc\n" | koshkit wc -lw'
echo "--- wc counts a directory as an empty input ---"
"$BIN" -c 'koshkit wc wc-width.txt . 2>&1; printf "status=%s\n" "$?"'
echo "--- nl leaves unnumbered lines without a separator ---"
printf 'a\n\nb\n' > nl-blank.txt
"$BIN" -c 'koshkit nl -b n -s ": " nl-blank.txt | od -An -c; koshkit nl -b t -s ": " nl-blank.txt | od -An -c'
echo "--- nl numbers several files as one stream ---"
"$BIN" -c 'koshkit nl nl-blank.txt missing.txt nl-blank.txt 2>&1; printf "status=%s\n" "$?"'
echo "--- unexpand with a tab list converts every blank run ---"
printf 'a\tb\n' > unexpand-tab.txt
printf 'a        b\n' > unexpand-spaces.txt
printf 'ab\t\tc\n' > unexpand-tabs.txt
"$BIN" -c 'for f in unexpand-tab.txt unexpand-spaces.txt unexpand-tabs.txt; do koshkit unexpand -t 2,6 "$f" | od -An -c; koshkit unexpand -a -t 2,6 "$f" | od -An -c; done'
echo "--- grep with an empty pattern file ---"
printf 'a\nb\n' > grep-lines.txt
"$BIN" -c 'koshkit grep -c -f empty.txt grep-lines.txt; printf "status=%s\n" "$?"; koshkit grep -vc -f empty.txt grep-lines.txt; printf "status=%s\n" "$?"'
echo "--- grep -E accepts a leading or dangling repetition ---"
printf 'a\n*a\nb+\na{1\n' > grep-repeat.txt
for pattern in '+' '*a' 'a|*b' '(+a)' '{1}a' 'a{1'; do
  "$BIN" -c 'koshkit grep -E "$1" grep-repeat.txt 2>/dev/null; printf "status=%s\n" "$?"' _ "$pattern"
done
echo "--- xargs -I unquotes the line and keeps inner blanks ---"
printf 'a b "c d" '"'"'e f'"'"' g\\ h\n  lead  trail  \nx\\\ny\n' > xargs-quoted.txt
"$BIN" -c "koshkit xargs -I{} echo '[{}]' < xargs-quoted.txt; printf 'status=%s\n' \"\$?\""
echo "--- xargs size limit statuses ---"
printf 'a b\n' | "$BIN" -c 'koshkit xargs -s 1 echo 2>&1; printf "status=%s\n" "$?"'
printf 'a b\n' | "$BIN" -c 'koshkit xargs -s 5 echo 2>&1; printf "status=%s\n" "$?"'
printf 'a bbbbbbbbbbbbbbbbbbbbbb\n' | "$BIN" -c 'koshkit xargs -s 8 echo 2>&1; printf "status=%s\n" "$?"'
echo "--- xargs separates exec failure from command status ---"
printf 'echo hi\n' > xargs-noexec.sh && chmod 644 xargs-noexec.sh
printf '#!/nonexistent/interpreter\n' > xargs-badshebang.sh && chmod 755 xargs-badshebang.sh
printf 'a\nb\n' | "$BIN" -c 'koshkit xargs -n 1 ./xargs-noexec.sh >/dev/null 2>&1; printf "status=%s\n" "$?"'
printf 'a\nb\n' | "$BIN" -c 'koshkit xargs -n 1 ./xargs-badshebang.sh >/dev/null 2>&1; printf "status=%s\n" "$?"'
printf 'a\nb\n' | "$BIN" -c 'koshkit xargs -n 1 sh -c "echo \$0; exit 127" 2>&1; printf "status=%s\n" "$?"'
printf 'a\nb\n' | "$BIN" -c 'koshkit xargs -n 1 sh -c "echo \$0; exit 126" 2>&1; printf "status=%s\n" "$?"'

echo "--- xargs stops only when its own command cannot run ---"
printf './xargs-noexec.sh 2>/dev/null; echo ok\n' > xargs-script && chmod 755 xargs-script
printf 'a\nb\n' | "$BIN" -c 'koshkit xargs -n 1 env ./xargs-noexec.sh 2>/dev/null; printf "status=%s\n" "$?"'
printf 'a\nb\n' | "$BIN" -c 'koshkit xargs -n 1 ./xargs-script; printf "status=%s\n" "$?"'

echo "--- unexpand copies blanks past the last tab stop ---"
printf 'abcde \tx\n' | "$BIN" -c 'koshkit unexpand -t 2,4' | od -c | sed -n 1p
printf 'abcde  \t x\n' | "$BIN" -c 'koshkit unexpand -t 2,4' | od -c | sed -n 1p

echo "--- xargs -I keeps empty quotes and rejects an open quote ---"
printf '"" x\n' | "$BIN" -c 'koshkit xargs -I{} echo "[{}]"'
printf '""\nq\n' | "$BIN" -c 'koshkit xargs -I{} echo "[{}]"'
printf '"abc' | "$BIN" -c 'koshkit xargs -I{} echo "[{}]" 2>/dev/null; printf "status=%s\n" "$?"'

echo "--- grep -E takes a repetition after ^ ---"
printf 'x\n*x\n' | "$BIN" -c 'koshkit grep -E "^*x"'
printf 'x\n+x\nax\n' | "$BIN" -c 'koshkit grep -E "^+x"'

echo "--- grep reports a match in a file with a NUL byte without its lines ---"
printf 'abc\0def\nxyz abc\n' > grep-binary.dat
"$BIN" -c 'koshkit grep abc grep-binary.dat fruit.txt; printf "status=%s\n" "$?"'
"$BIN" -c 'koshkit grep -c abc grep-binary.dat'
"$BIN" -c 'koshkit grep zzz grep-binary.dat; printf "status=%s\n" "$?"'
printf 'q\0abc\n' | "$BIN" -c 'koshkit grep abc'
printf 'abc\nabc tail\nzz\0\nabc after\n' > grep-late-nul.dat
"$BIN" -c 'koshkit grep abc grep-late-nul.dat'
mkdir grep-binary-dir
printf 'abc\0\n' > grep-binary-dir/b.dat
"$BIN" -c 'koshkit grep -r abc grep-tree grep-binary-dir'
"$BIN" -c 'koshkit grep -a abc grep-binary.dat' | od -c | sed -n 1,2p

echo "--- seq counts long integers exactly ---"
"$BIN" -c 'koshkit seq 99999999999999999999 100000000000000000001'
"$BIN" -c 'koshkit seq 18446744073709551615 18446744073709551617'
"$BIN" -c 'koshkit seq 0000000000000000003'
"$BIN" -c 'koshkit seq 1e-99999999999999999999 1 1 | koshkit wc -c'

echo "--- nl and od take widths up to 65536 ---"
"$BIN" -c 'printf "a\n" | koshkit nl -w 65536 | koshkit wc -c'
"$BIN" -c 'koshkit od -w65537 /dev/null 2>&1; printf "status=%s\n" "$?"'

echo "--- comm reports one disorder for a paired line ---"
printf 'b\na\n' > disorder
"$BIN" -c 'koshkit comm --check-order disorder disorder 2>/dev/null; printf "status=%s\n" "$?"'

echo "--- sort -m merges the sources as given ---"
printf 'c\na\nb\n' > merge1
printf 'b\nd\na\n' > merge2
"$BIN" -c 'koshkit sort -m merge1 merge2'

echo "--- tr maps past a long set1 repeat ---"
echo ab | "$BIN" -c "koshkit tr '[a*70000]b' '[x*69999]yz'"
