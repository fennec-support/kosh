unset KOSH_FLAGS
# Each bad substitution says which part makes it bad, points the excerpt at
# that part, and suggests the form that was likely meant. Every form runs in
# its own bash mood shell, which abandons the line and keeps status 1. The
# list forms run inside [[ ]], where they expand to one string, and the last
# form sits in a here-document, whose errors point into the body.
check()
{
  "$BIN" --mood bash --no-diagnostics -c "x=1; a=(1); echo \"$1\"" 2>&1
  echo "status=$?"
}

check_list()
{
  "$BIN" --mood bash --no-diagnostics -c "a=(1); [[ $1 ]]" 2>&1
  echo "status=$?"
}

check '${}'
check '${%}'
check '${:-a}'
check '${x!y}'
check '${x:}'
check '${x.y}'
check '${x[}'
check '${1x}'
check '${?x}'
check '${a[0]!}'
check '${x@Z}'
check '${x@}'
check_list '${a[@]:}'
check_list '${a[@]x}'
check_list '${a[@]@QQ}'
"$BIN" --mood bash --no-diagnostics -c 'a=(); [[ ${a[@]:=x} ]]' 2>&1
echo "status=$?"
"$BIN" --mood bash --no-diagnostics -c 'cat <<E
body ${x!y}
E
echo "status=$?"' 2>&1
