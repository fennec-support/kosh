unset KOSH_FLAGS
# kill, fg, disown, bg, and wait accept the full job spec grammar, not only
# %number. A leading name matches a job by its command prefix, %+ selects the
# current job. Each accepted spec reports status 0 without a diagnostic.
echo "== kill resolves a job by its command name:"
"$BIN" --mood bash -c 'sleep 3 & kill %sleep; echo rc=$?; wait' 2>&1
echo "== disown by name drops the job so a later kill fails:"
"$BIN" --mood bash -c 'sleep 3 & disown %sleep; kill %1' 2>&1 | grep -c "not a valid job\|not a known job"
echo "== fg by %+ selects the current job:"
"$BIN" --mood bash -c 'sleep 0.2 & fg %+; echo rc=$?' 2>&1
echo "== wait resolves a job by its command name:"
"$BIN" --mood bash -c 'sleep 0.2 & wait %sleep; echo rc=$?' 2>&1
echo "== bg by name resumes a stopped job:"
"$BIN" --mood bash -c 'sleep 0.3 & kill -STOP %sleep; bg %sleep; echo rc=$?; wait' 2>&1
echo "== bg refuses a job that has already finished:"
"$BIN" --mood bash -c 'sleep 0.1 & sleep 0.5; msg=$(bg %1 2>&1); echo rc=$?; echo "$msg" | sed -n 1p' 2>&1
