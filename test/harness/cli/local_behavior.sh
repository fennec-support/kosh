unset KOSH_FLAGS
export KOSH_FLAGS=--no-diagnostics
# local rejects use outside a function and invalid names.
echo "== local outside a function is an error:"
{
    "$BIN" -c 'local x=5' 2>&1
    printf 'rc=%s\n' "$?"
} | "invoke-normalize-trace" "$BIN"
echo "== invalid local names fail:"
"$BIN" -c 'f(){
  local bad-name "" 3name 2>/dev/null
  echo "status=$?"
}; f'

unset KOSH_FLAGS
# The no-local warning fires for a fresh name assigned in a function body, the
# leaking footgun, but stays quiet for a name already set at the top level or
# inherited from the environment, since that assignment updates an existing
# variable rather than leaking a new one.
echo "== fresh name warns:"
"$BIN" -W -c 'fn(){ brandnewname=1; }; fn' 2>&1 | grep -c 'has no .local.'
echo "== top-level name is quiet:"
"$BIN" -W -c 'seeded=0; fn(){ seeded=1; }; fn' 2>&1 | grep -c 'has no .local.'
echo "== inherited PATH is quiet:"
"$BIN" -W -c 'fn(){ PATH="${PATH:+$PATH$TEST_PATH_SEPARATOR}/x"; }; fn' 2>&1 | grep -c 'has no .local.'
echo "rc=$?"
