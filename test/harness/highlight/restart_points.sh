set -e

# The highlight scan records a restart point at a line start with no open
# construct at least every 4096 bytes. A scan that resumes at one of them must
# color the rest exactly as a scan from the start, including functions and
# variables defined before the point and constructs that span several lines.
script='early_fn() { echo "$1"; }
assigned_early=1
'
block='case $x in
  a) early_fn a ;;
  *) echo "$assigned_early" $unknown ;;
esac
arr=(one
two three)
cat <<END
body $x
END
cat <<"Q E"
literal
Q E
for i in 1 2; do
  early_fn "$i"
done
x=$(echo a |
  tr a b)
s="multi
line string"
if [[ -f x && $y == z ]]; then
  :
fi
echo $((1 +
 2)) `date` ${var:-default}
early_fn arg # comment ( { [
'
i=0
body=
while [ "$i" -lt 40 ]; do
  body=$body$block
  i=$((i + 1))
done

# A prefix that grows by about 12 bytes per run moves the restart points
# across every line of the block.
pad=
run=0
while [ "$run" -lt 24 ]; do
  "$BIN" --debug-highlight-restarts-at "$pad$script$body"
  pad="$pad# padding $run
"
  run=$((run + 1))
done
