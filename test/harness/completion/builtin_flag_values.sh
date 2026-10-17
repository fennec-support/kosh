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
echo "== compopt -o and +o options:"
"$BIN" --debug-complete-at 'compopt -o pl' </dev/null
"$BIN" --debug-complete-at 'compopt +o no' </dev/null
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
echo "== set has no long mood flags:"
"$BIN" --debug-complete-at 'set --m' </dev/null
"$BIN" --debug-complete-at 'set --i' </dev/null
echo "== set -M value:"
"$BIN" --debug-complete-at 'set -M ' </dev/null
echo "== set -L value:"
"$BIN" --debug-complete-at 'set -L ' </dev/null
echo "== set -M= value:"
"$BIN" --debug-complete-at 'set -M=' </dev/null
echo "== set -M= prefix:"
"$BIN" --debug-complete-at 'set -M=b' </dev/null
echo "== kosh --mood value:"
"$BIN" --debug-complete-at 'kosh --mood ' </dev/null
echo "== kosh --mood= value:"
"$BIN" --debug-complete-at 'kosh --mood=' </dev/null
echo "== kosh -M value:"
"$BIN" --debug-complete-at 'kosh -M ' </dev/null
echo "== kosh --init-moods value:"
"$BIN" --debug-complete-at 'kosh --init-moods ' </dev/null
echo "== kosh -L= prefix:"
"$BIN" --debug-complete-at 'kosh -L=k' </dev/null
echo "== assimilate --link-mood value:"
"$BIN" --debug-complete-at 'assimilate --link-mood ' </dev/null
echo "== assimilate --link-mood= prefix:"
"$BIN" --debug-complete-at 'assimilate --link-mood=d' </dev/null
echo "== assignment prefix is untouched:"
result=$("$BIN" --debug-complete-at 'MOOD=' </dev/null) || exit 1
printf '%s\n' "$result" | grep -qxF bash
[ "$?" -eq 1 ] || exit 1
printf '0\n'
echo "== koshconf set completion.menu_style value:"
"$BIN" --debug-complete-at 'koshconf set completion.menu_style ' \
  </dev/null
echo "== koshconf set completion.menu_style prefix:"
"$BIN" --debug-complete-at 'koshconf set completion.menu_style e' \
  </dev/null
echo "== kosh --tab-selector value:"
"$BIN" --debug-complete-at 'kosh --tab-selector ' </dev/null
echo "== kosh --tab-selector= prefix:"
"$BIN" --debug-complete-at 'kosh --tab-selector=p' </dev/null
echo "== the flag name itself:"
"$BIN" --debug-complete-at 'kosh --tab-s' </dev/null
echo "== set has no tab selector flag:"
"$BIN" --debug-complete-at 'set --tab-s' </dev/null
echo "== an unrelated command keeps its own candidates:"
result=$("$BIN" --debug-complete-at 'echo --tab-selector ' </dev/null) || exit 1
printf '%s\n' "$result" | grep -qxF interactive
[ "$?" -eq 1 ] || exit 1
printf '0\n'
