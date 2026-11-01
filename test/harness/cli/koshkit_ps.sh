# The ps listing shows live processes, so the rows differ on every machine and
# only the layout is asserted. The plain header is fixed, and the aux header and
# the row of the running shell are matched rather than printed in full.
unset KOSH_FLAGS

echo "--- plain ps header ---"
"$BIN" -c 'koshkit ps' | head -1

echo "--- aux and -aux headers have the USER and COMMAND columns ---"
for ps_form in aux -aux; do
  if "$BIN" -c "koshkit ps $ps_form" | head -1 | grep -qE '^USER +PID +VSZ +RSS STAT COMMAND$'; then
    echo "header ok"
  else
    echo "header bad"
  fi
done

echo "--- aux lists the row of the running shell ---"
if [ "$("$BIN" -c 'koshkit ps aux | koshkit grep -cE "^[^ ]+ +$$ "')" = 1 ]; then
  echo "self ok"
else
  echo "self missing"
fi

echo "--- ps -ef uses the full columns ---"
"$BIN" -c 'koshkit ps -ef' | head -1 | tr -s ' '

echo "--- -o and -p select one headerless row ---"
"$BIN" -c 'koshkit ps -o pid,ppid --no-headers -p $$ | koshkit wc -l' |
  tr -d ' '

echo "--- a process id that selects nothing ---"
"$BIN" -c 'koshkit ps -p 2147483646 --no-headers'
echo "status $?"

echo "--- a command that selects nothing ---"
"$BIN" -c 'koshkit ps -C no-such-command --no-headers'
echo "status $?"

echo "--- an unknown column ---"
"$BIN" -c 'koshkit ps -o nope' >/dev/null 2>&1
echo "status $?"
