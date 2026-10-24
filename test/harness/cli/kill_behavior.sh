unset KOSH_FLAGS
# The kill builtin lists SIGKILL under --list, and the killall and pkill
# utilities list signals under -l, all through one shared formatter that uses
# the platform signal table.
echo "== kill --list lists SIGKILL (count):"
"$BIN" -c 'kill --list' 2>&1 | grep -c ') SIGKILL'
echo "== killall -l lists SIGHUP (count):"
"$BIN" -c 'koshkit killall -l' </dev/null 2>&1 | grep -c ') SIGHUP'
echo "== pkill -l lists SIGINT (count):"
"$BIN" -c 'koshkit pkill -l' </dev/null 2>&1 | grep -c ') SIGINT'

unset KOSH_FLAGS
# kill rejects a missing target, an unknown signal name, an unknown job, and a
# non-numeric target rather than falling through to a process-group signal. No
# real signal is delivered, every case is an error path with a fixed message.
echo "== no target is required:"; "$BIN" -c 'kill'; echo "rc=$?"
echo "== an unknown signal name is rejected:"; "$BIN" -c 'kill -BOGUSSIG 123'; echo "rc=$?"
echo "== an unknown job is reported:"; "$BIN" -c 'kill %999'; echo "rc=$?"
echo "== a non-numeric job is rejected:"; "$BIN" -c 'kill %abc'; echo "rc=$?"
echo "== a non-numeric pid is rejected:"; "$BIN" -c 'kill notanumber'; echo "rc=$?"
