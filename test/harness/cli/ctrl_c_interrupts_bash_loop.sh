# shellcheck disable=unassigned-variable-read
unset KOSH_FLAGS

"$BIN" -c \
  'koshkit timeout -p -s INT -k 8s 2s "$1" -c "$2" arithmetic 3; echo "arithmetic-exit=$?"' \
  signaler "$BIN" 'n=$1; echo $((n ** 100000000))' 2>&1

"$BIN" -c 'koshkit timeout -p -s INT -k 3 "$@"' timeout 1 "$BIN" --mood bash -c 'while true; do for ((j=0;j<100000;j++)); do :; done; done' </dev/null >/dev/null 2>&1
status=$?
if [ "$status" -eq 130 ]; then
  echo "exit=interrupted"
else
  echo "exit=$status"
fi
