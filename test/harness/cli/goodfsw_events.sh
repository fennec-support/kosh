unset KOSH_FLAGS
# On Linux, goodfsw scans after an inotify event instead of waiting for the
# quiet-tree rescan, which runs only after 30 latency periods. A directory
# replaced at the same path is watched again, so a file created inside it is
# reported before that rescan. With -x, a file created in a subdirectory on the
# same filesystem is still reported.
root=$TEST_MKTEMP_DIRECTORY/goodfsw-events
output=$TEST_MKTEMP_DIRECTORY/goodfsw-events.out
mkdir -p "$root/sub" || exit 1
: > "$output"

timeout -s INT 4 "$BIN" -c 'koshkit goodfsw -r -l 0.3 "$1"' goodfsw "$root" \
  > "$output" 2>&1 &
watcher_pid=$!

sleep 0.6
mv "$root/sub" "$root/sub.old"
mkdir "$root/sub"
sleep 1.2
: > "$root/sub/late"
wait "$watcher_pid"
echo "status=$?"

echo "== the file created inside the replaced directory is reported:"
grep -c " $root/sub/late\$" "$output"

one_fs_root=$TEST_MKTEMP_DIRECTORY/goodfsw-one-fs
one_fs_output=$TEST_MKTEMP_DIRECTORY/goodfsw-one-fs.out
mkdir -p "$one_fs_root/inner" || exit 1
: > "$one_fs_output"

timeout -s INT 4 "$BIN" -c 'koshkit goodfsw -r -1 -l 0.05 -x "$1"' goodfsw \
  "$one_fs_root" > "$one_fs_output" 2>&1 &
watcher_pid=$!

sleep 0.6
: > "$one_fs_root/inner/made"
wait "$watcher_pid"
echo "one-fs-status=$?"

echo "== -x reports a file created on the same filesystem:"
grep -c " $one_fs_root/inner/made\$" "$one_fs_output"
