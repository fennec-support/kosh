unset KOSH_FLAGS
# The sh mood and the bash-posix mood that --posix selects list a running
# background job with the jobs builtin and wait for it.
echo "== sh mood lists a running background job and waits for it:"
"$BIN" --mood sh -c 'sleep 0.2 & jobs; wait; echo DONE' 2>&1
echo "rc=$?"
echo "== --posix lists a running background job and waits for it:"
"$BIN" --posix -c 'sleep 0.2 & jobs; wait; echo DONE' 2>&1
echo "rc=$?"
