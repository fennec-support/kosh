unset KOSH_FLAGS

echo "--- selected values ---"
KOSH_PRINTENV_A=one KOSH_PRINTENV_EMPTY= \
  "$BIN" -c 'koshkit printenv KOSH_PRINTENV_A KOSH_PRINTENV_EMPTY KOSH_PRINTENV_MISSING; echo status=$?'

echo "--- complete environment ---"
KOSH_PRINTENV_A=one \
  "$BIN" -c 'koshkit printenv | koshkit grep "^KOSH_PRINTENV_A=one$"; echo status=$?'

echo "--- NUL-terminated values ---"
KOSH_PRINTENV_A=one \
  "$BIN" -c 'koshkit printenv -0 KOSH_PRINTENV_A KOSH_PRINTENV_A | koshkit od -An -c'
KOSH_PRINTENV_A=one \
  "$BIN" -c 'koshkit printenv -0 | koshkit grep -z "^KOSH_PRINTENV_A=one$" | koshkit od -An -c'

echo "--- missing value ---"
"$BIN" -c 'koshkit printenv KOSH_PRINTENV_MISSING; echo status=$?'
