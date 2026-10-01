unset KOSH_FLAGS
echo "== -W is silent on an unset :+:"
result=$("$BIN" -W -c 'echo "[${UNSETV:+x}]"' 2>&1) || exit 1
[ "$result" = '[]' ] || exit 1
printf '0\n'
echo "== plain run is silent:"
result=$("$BIN" --mood bash -c 'echo "[${UNSETV:+x}]"' 2>&1) || exit 1
[ "$result" = '[]' ] || exit 1
printf '0\n'
echo "== a set name is silent under -W:"
result=$("$BIN" -W -c 'V=val; echo "[${V:+y}]"' 2>&1) || exit 1
[ "$result" = '[y]' ] || exit 1
printf '0\n'
