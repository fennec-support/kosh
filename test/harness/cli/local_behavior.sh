unset KOSH_FLAGS
export KOSH_FLAGS=--no-diagnostics
# local rejects invalid identifiers.
echo "== invalid local identifiers fail:"
"$BIN" -c 'f(){
  local bad-name "" 3name 2>/dev/null
  echo "status=$?"
}; f'

unset KOSH_FLAGS
# The no-local warning fires for a fresh name assigned in a function body, the
# leaking footgun, and reports the location of the assignment.
echo "== fresh name warns:"
"$BIN" -W -c 'fn(){ brandnewname=1; }; fn' 2>&1
