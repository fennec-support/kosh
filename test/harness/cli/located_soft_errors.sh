unset KOSH_FLAGS
# A builtin that cannot find the name it was given points at the whole
# statement, and koshkit timeout points at the missing command. These rows run
# on every platform; granular_arg_caret owns the argument carets.

echo "== alias points at the whole statement:"
"$BIN" -c 'alias missing_name' 2>&1
echo "rc=$?"
echo "== hash points at the whole statement:"
"$BIN" -c 'hash missing_name' 2>&1
echo "rc=$?"
echo "== command -V points at the whole statement:"
"$BIN" -c 'command -V missing_name' 2>&1
echo "rc=$?"
echo "== jobs points at the whole statement:"
"$BIN" -c 'jobs %999' 2>&1
echo "rc=$?"
echo "== timeout points at the missing command:"
"$BIN" -c 'koshkit timeout 1 missing_name' 2>&1
echo "rc=$?"
