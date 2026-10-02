# Tail follows regular files after the initial output. Appended bytes, truncation,
# rename rotation, replacement, header switches, a watched process, and the
# pipe and interrupt exits match GNU tail. Every wait is bounded.
unset KOSH_FLAGS
export KOSH_FLAGS=--no-diagnostics
d=$(mktemp -d) || exit 1
follower=
cleanup()
{
  if [ -n "$follower" ]; then
    kill -KILL "$follower" 2>/dev/null
    wait "$follower" 2>/dev/null
  fi
  if [ -n "$d" ]; then
    invoke-koshkit rm -r "$d"
  fi
}
trap cleanup EXIT

wait_for_text()
{
  wait_count=0
  while ! grep -F -q -- "$1" "$2" 2>/dev/null; do
    wait_count=$((wait_count + 1))
    if [ "$wait_count" -gt 300 ]; then
      echo "timed out waiting for '$1' in $2"
      return 1
    fi
    sleep 0.1
  done
}
start_follower()
{
  : > "$d/out"
  : > "$d/err"
  "$BIN" -c 'koshkit tail "$@"' koshkit "$@" > "$d/out" 2> "$d/err" &
  follower=$!
}
stop_follower()
{
  kill -TERM "$follower" 2>/dev/null
  wait "$follower" 2>/dev/null
  follower=
  sed "s#$d#TMPDIR#g" "$d/out"
  echo "stderr:"
  sed -n 's/^.*koshkit tail: //p' "$d/err" | sed "s#$d#TMPDIR#g"
}

echo "--- descriptor follow: append, truncate, rename ---"
printf 'one\n' > "$d/a.txt"
start_follower -f -s 0.05 "$d/a.txt"
wait_for_text one "$d/out"
printf 'two\nthree\n' >> "$d/a.txt"
wait_for_text three "$d/out"
printf 'fresh\n' > "$d/a.txt"
wait_for_text fresh "$d/out"
mv "$d/a.txt" "$d/a.old"
printf 'renamed\n' >> "$d/a.old"
wait_for_text renamed "$d/out"
stop_follower

echo "--- name follow: rotation and replacement ---"
printf 'first\n' > "$d/r.txt"
start_follower -F -s 0.05 "$d/r.txt"
wait_for_text first "$d/out"
mv "$d/r.txt" "$d/r.old"
wait_for_text 'has become inaccessible' "$d/err"
printf 'second\n' > "$d/r.txt"
wait_for_text second "$d/out"
printf 'third\n' > "$d/r.new"
mv "$d/r.new" "$d/r.txt"
wait_for_text third "$d/out"
stop_follower

echo "--- name follow without retry drops a deleted file ---"
printf 'gone\n' > "$d/g.txt"
start_follower --follow=name -s 0.05 "$d/g.txt"
wait_for_text gone "$d/out"
invoke-koshkit rm "$d/g.txt"
wait_for_text 'no files remaining' "$d/err"
wait "$follower"
echo "status=$?"
follower=
cat "$d/out"
sed -n 's/^.*koshkit tail: //p' "$d/err" | sed "s#$d#TMPDIR#g"

echo "--- retry opens a file that appears later ---"
start_follower -f --retry -s 0.05 "$d/late.txt"
wait_for_text 'cannot open' "$d/err"
printf 'late\n' > "$d/late.txt"
wait_for_text late "$d/out"
stop_follower

echo "--- headers switch with the file that printed last ---"
printf '1a\n' > "$d/h1.txt"
printf '2a\n' > "$d/h2.txt"
start_follower -f -s 0.05 "$d/h1.txt" "$d/h2.txt"
wait_for_text 2a "$d/out"
printf '2b\n' >> "$d/h2.txt"
wait_for_text 2b "$d/out"
printf '1b\n' >> "$d/h1.txt"
wait_for_text 1b "$d/out"
printf '1c\n' >> "$d/h1.txt"
wait_for_text 1c "$d/out"
stop_follower
echo "--- quiet and verbose headers ---"
start_follower -q -f -s 0.05 "$d/h1.txt" "$d/h2.txt"
wait_for_text 2a "$d/out"
printf '1d\n' >> "$d/h1.txt"
wait_for_text 1d "$d/out"
stop_follower
start_follower -v -f -s 0.05 "$d/h1.txt"
wait_for_text 1d "$d/out"
stop_follower

echo "--- zero lines still follows ---"
start_follower -n 0 -f -s 0.05 "$d/h1.txt" "$d/h2.txt"
wait_for_text h2.txt "$d/out"
printf '1e\n' >> "$d/h1.txt"
wait_for_text 1e "$d/out"
stop_follower

echo "--- regular standard input is followed ---"
printf 'in1\n' > "$d/in.txt"
: > "$d/out"
"$BIN" -c 'koshkit tail -f -s 0.05' < "$d/in.txt" > "$d/out" 2> "$d/err" &
follower=$!
wait_for_text in1 "$d/out"
printf 'in2\n' >> "$d/in.txt"
wait_for_text in2 "$d/out"
stop_follower

echo "--- pipe standard input ignores follow ---"
printf 'p1\np2\n' | invoke-koshkit timeout 20 "$BIN" -c 'koshkit tail -f -n 1'
echo "status=$?"

echo "--- missing file without retry ---"
invoke-koshkit timeout 20 "$BIN" -c 'koshkit tail -f "$1"' x "$d/none.txt" 2>&1 \
  | sed -n 's/^.*koshkit tail: //p' | sed "s#$d#TMPDIR#g"

echo "--- watched process ends the follow ---"
printf 'w1\n' > "$d/w.txt"
watched_pid=$(sh -c 'sleep 1 >/dev/null 2>&1 & echo $!')
invoke-koshkit timeout 30 "$BIN" -c 'koshkit tail -f -s 0.05 --pid "$1" "$2"' x \
  "$watched_pid" "$d/w.txt"
echo "status=$?"

echo "--- invalid operands ---"
"$BIN" -c 'koshkit tail -s x "$1"' x "$d/w.txt" 2>&1 | sed -n 's/^.*error: //p'
"$BIN" -c 'koshkit tail --pid x "$1"' x "$d/w.txt" 2>&1 | sed -n 's/^.*error: //p'

echo "--- interrupt ends the wait ---"
invoke-koshkit timeout -p -s INT -k 10s 2s "$BIN" -c 'koshkit tail -f -s 60 "$1"' x "$d/w.txt"
echo "status=$?"
