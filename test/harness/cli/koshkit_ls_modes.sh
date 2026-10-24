# The ls listing modes run inside a fresh temporary directory so the output has
# no absolute path and stays the same on every machine. The binary path is
# resolved to an absolute one first, since the working directory changes.
unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

mkdir -p sub/deep empty sized .symlink-batch
printf 'aaa\n' > plain.txt
printf '#!/bin/sh\n' > run.sh
chmod +x run.sh
: > nowhere
: > .symlink-batch/nowhere
ln -s plain.txt good-link
ln -s nowhere bad-link
for link_index in 1 2 3 4; do
  ln -s ../plain.txt ".symlink-batch/good-$link_index"
  ln -s nowhere ".symlink-batch/bad-$link_index"
done
rm nowhere .symlink-batch/nowhere
: > sub/inner.txt
: > sub/deep/leaf.txt
# The size sort uses regular files alone, because the size a directory reports
# differs between filesystems.
printf 'a\n' > sized/small
printf 'bbbbb\n' > sized/medium
printf 'cccccccccc\n' > sized/large
: > .hidden.txt
mkdir .hidden-dir
: > sized/tie-a
: > sized/tie-b
long_name=abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz.txt
: > "$long_name"

echo "--- plain ---"
"$BIN" -c 'koshkit ls'
echo "--- all entries ---"
"$BIN" -c 'koshkit ls -a'
echo "--- almost all entries ---"
"$BIN" -c 'koshkit ls -A'
echo "--- multiple roots ---"
"$BIN" -c 'koshkit ls plain.txt sub'
echo "--- long name ---"
"$BIN" -c 'koshkit ls -1' | grep "$long_name"
echo "--- classify ---"
"$BIN" -c 'koshkit ls -F'
echo "--- mark directories only ---"
"$BIN" -c 'koshkit ls -p'
echo "--- directory operands are listed as themselves ---"
"$BIN" -c 'koshkit ls -d sub plain.txt'
"$BIN" -c 'koshkit ls -d'
"$BIN" -c 'koshkit ls -dp sub'
echo "--- numeric and partial long forms ---"
"$BIN" -c 'koshkit ls -dn plain.txt | koshkit grep "^-[rwx-]* *1 [0-9][0-9]* [0-9][0-9]* *4 " | koshkit wc -l'
"$BIN" -c 'koshkit ls -dg plain.txt | koshkit cut -d " " -f 4'
"$BIN" -c 'koshkit ls -do plain.txt | koshkit cut -d " " -f 4'
echo "--- reverse name ---"
"$BIN" -c 'koshkit ls -r'
echo "--- sort by size ---"
"$BIN" -c 'koshkit ls -S sized'
echo "--- sort by size reversed ---"
"$BIN" -c 'koshkit ls -Sr sized'
echo "--- human total ---"
human_total=$("$BIN" -c 'koshkit ls -lah sized' | head -n 1)
case $human_total in
  total\ *[KMGTP]) echo "human-total=matched" ;;
  *) echo "human-total=wrong" ;;
esac
echo "--- tree ---"
"$BIN" -c 'koshkit ls --tree sub'
echo "--- tree bounded to one level ---"
"$BIN" -c 'koshkit ls --tree -L 1 sub'
echo "--- recursive ---"
"$BIN" -c 'koshkit ls -R sub'
echo "--- recursive bounded to one level ---"
"$BIN" -c 'koshkit ls -R -L 1 sub'
echo "--- recursive reaches an empty directory ---"
"$BIN" -c 'koshkit ls -R empty'
echo "--- one file system changes nothing on one file system ---"
"$BIN" -c 'koshkit ls -R --one-file-system sub'
"$BIN" -c 'koshkit ls --tree --one-file-system sub'
if [ "${TARGET:-$(uname -s)}" = Linux ] && [ -d /dev/pts ]; then
  plain_tree=$("$BIN" -c 'koshkit ls --tree -L 2 /dev' 2>/dev/null)
  bounded_tree=$("$BIN" -c 'koshkit ls --tree -L 2 --one-file-system /dev' 2>/dev/null)
  plain_recursive=$("$BIN" -c 'koshkit ls -R -L 2 /dev' 2>/dev/null)
  bounded_recursive=$("$BIN" -c 'koshkit ls -R -L 2 --one-file-system /dev' 2>/dev/null)
  case $plain_recursive in
    */dev/pts:*) ;;
    *) echo "ls-one-file-system=plain-recursive-missing-pts" ;;
  esac
  case $bounded_recursive in
    */dev/pts:*) echo "ls-one-file-system=recursive-entered-pts" ;;
  esac
  case $bounded_recursive in
    */dev:*) ;;
    *) echo "ls-one-file-system=recursive-lost-operand" ;;
  esac
  if [ "$plain_tree" = "$bounded_tree" ]; then
    echo "ls-one-file-system=tree-not-bounded"
  fi
fi
echo "--- invalid level ---"
"$BIN" -c 'koshkit ls -L 0 sub' 2>/dev/null
echo "rc=$?"
echo "--- unreadable root ---"
mkdir unreadable
: > unreadable/entry
chmod 000 unreadable
if ls unreadable >/dev/null 2>&1; then
  readable_output=$("$BIN" -c 'koshkit ls unreadable' 2>&1)
  readable_status=$?
  chmod 700 unreadable
  case $readable_output in
    *entry*)
      if [ "$readable_status" -eq 0 ]; then
        echo "ls-unreadable=checked"
      else
        echo "ls-unreadable=failed"
      fi
      ;;
    *) echo "ls-unreadable=failed" ;;
  esac
else
  unreadable_output=$("$BIN" -c 'koshkit ls unreadable' 2>&1)
  unreadable_status=$?
  chmod 700 unreadable
  case $unreadable_output in
    *entry*) echo "ls-unreadable=failed" ;;
    *)
      if [ "$unreadable_status" -ne 0 ]; then
        echo "ls-unreadable=checked"
      else
        echo "ls-unreadable=failed"
      fi
      ;;
  esac
fi
echo "--- recursive ls interruption ---"
if [ "${TARGET:-$(uname -s)}" = Linux ]; then
  interrupt_root=$TEST_TEMP_DIRECTORY/ls-interrupt
  "$BIN" -c 'koshkit mkdir -p "$1"' mkdir "$interrupt_root"
  interrupt_directory=0
  while [ "$interrupt_directory" -lt 200 ]; do
    interrupt_path=$interrupt_root/d$interrupt_directory
    "$BIN" -c 'koshkit mkdir "$1"' mkdir "$interrupt_path"
    (
      cd "$interrupt_path" || exit 1
      seq 1 200 | xargs touch
    )
    interrupt_directory=$((interrupt_directory + 1))
  done
  interrupt_expected_count=$("$BIN" -c 'koshkit ls -R "$1"' ls "$interrupt_root" |
    wc -l)
  interrupt_status=$("$BIN" --no-traces --mood bash -c '
    shell_pid=$$
    (
      while [ ! -e ls-interrupt.ready ]; do /bin/sleep 0.001; done
      /bin/sleep 0.005
      kill -INT $shell_pid
    ) &
    : > ls-interrupt.ready
    koshkit ls -R "$1" > ls-interrupt.out 2> ls-interrupt.err
    ls_status=$?
    wait
    exit $ls_status
  ' ls-interrupt "$interrupt_root" 2> /dev/null; echo "$?")
  interrupt_line_count=$(wc -l < ls-interrupt.out)
  if [ "$interrupt_status" -eq 130 ] &&
    [ "$interrupt_line_count" -lt "$interrupt_expected_count" ] &&
    ! grep -q 'Interrupted system call' ls-interrupt.err; then
    :
  else
    echo "ls-interrupt=failed status=$interrupt_status lines=$interrupt_line_count"
  fi
fi

echo "== long and classified listings of a directory operand:"
"$BIN" -c 'koshkit ls -l sized | koshkit grep -v "^total" | koshkit cut -c 1'
"$BIN" -c 'koshkit ls -F sub'

cd / || exit 1
[ -n "$d" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$d"
