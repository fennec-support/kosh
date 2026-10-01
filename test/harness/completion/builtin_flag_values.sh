echo "== kill -s signal names:"
signal_names=$("$BIN" --debug-complete-at 'kill -s ' </dev/null)
printf '%s\n' "$signal_names" | grep -E '^(KILL|TERM)$'
if [ "${OS-}" != Windows_NT ]; then
  for signal_name in HUP INT QUIT; do
    printf '%s\n' "$signal_names" | grep -qxF "$signal_name" ||
      printf 'missing-signal=%s\n' "$signal_name"
  done
fi
echo "== kill -s prefix:"
"$BIN" --debug-complete-at 'kill -s KI' </dev/null
echo "== kill -n prefix:"
"$BIN" --debug-complete-at 'kill -n TE' </dev/null
echo "== trap special conditions:"
"$BIN" --debug-complete-at 'trap handler E' </dev/null | grep -E '^(ERR|EXIT)$'
echo "== trap condition after the print flag:"
"$BIN" --debug-complete-at 'trap -p RET' </dev/null
echo "== trap action position is a filename:"
"$BIN" --debug-complete-at 'trap ' </dev/null | grep -c '^EXIT$'
echo "== shopt -o reads the set option names:"
"$BIN" --debug-complete-at 'shopt -o pipe' </dev/null
echo "== shopt operand stays a shopt name:"
"$BIN" --debug-complete-at 'shopt xpg' </dev/null
echo "== enable builtin names:"
"$BIN" --debug-complete-at 'enable ech' </dev/null
echo "== complete -o options:"
"$BIN" --debug-complete-at 'complete -o ' </dev/null
echo "== compgen -o joined form:"
"$BIN" --debug-complete-at 'compgen -o=d' </dev/null
echo "== compgen -V names a variable:"
"$BIN" -c 'completion_probe_var=1' --debug-complete-at 'compgen -V completion_probe' </dev/null
echo "== compgen -V joined form:"
"$BIN" --debug-complete-at 'compgen -V=KOSH_ANSI_RE' </dev/null
echo "== koshkit find entry types:"
"$BIN" --debug-complete-at 'koshkit find -type ' </dev/null
echo "== koshkit timeout signal prefix:"
"$BIN" --debug-complete-at 'koshkit timeout -s KI' </dev/null
echo "== koshkit pkill joined signal form:"
"$BIN" --debug-complete-at 'koshkit pkill --signal=TE' </dev/null
echo "== koshkit killall signal prefix:"
"$BIN" --debug-complete-at 'koshkit killall -s KI' </dev/null
echo "== koshkit evilio sort values:"
"$BIN" --debug-complete-at 'koshkit evilio --sort read-' </dev/null
echo "== bare evilio sort values:"
"$BIN" --debug-complete-at 'evilio --sort write-' </dev/null
echo "== koshkit evilps sort prefix:"
"$BIN" --debug-complete-at 'koshkit evilps --sort m' </dev/null
echo "== debug logging levels:"
"$BIN" --debug-complete-at 'kosh -X ' </dev/null
echo "== debug logging joined form:"
"$BIN" --debug-complete-at 'kosh --debug-logging=de' </dev/null
echo "== every signal flag reads the platform table:"
signal_name=$("$BIN" --debug-complete-at 'kill -s ' </dev/null | tail -n 1)
signal_prefix=$(printf '%s' "$signal_name" | cut -c1-2)
agreement=yes
[ -n "$signal_name" ] || agreement=no
for probe in "kill -s $signal_prefix" "trap handler $signal_prefix" \
  "koshkit timeout --signal=$signal_prefix"; do
  "$BIN" --debug-complete-at "$probe" </dev/null |
    grep -qxF "$signal_name" || agreement=no
done
echo "$agreement"
