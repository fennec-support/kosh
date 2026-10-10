#!/bin/bash
# noclobber refuses only an existing regular file, so a device and a FIFO
# still take output. A descriptor number past the 32-bit range is a bad
# descriptor instead of wrapping onto a small one, and n>&m- moves m onto n,
# closing m.
work_dir=$(mktemp -d)
cd "$work_dir" || exit 1

set -C
echo device > /dev/null
echo "device=$?"
echo first >| regular
echo second > regular 2> /dev/null
echo "regular=$?"
cat regular
mkfifo fifo
cat fifo > /dev/null &
echo through > fifo
echo "fifo=$?"
wait
set +C

echo wrapped >&4294967297 2> /dev/null
echo "huge=$?"
echo moved 4>&1 1>&4-
echo "move=$?"
exec 4>&1
exec 5>&4-
if { echo probe >&4; } 2> /dev/null; then
  echo four-open
else
  echo four-closed
fi
echo via-five >&5
exec 5>&-

cd / || exit 1
rm -r "$work_dir"
