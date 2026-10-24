unset KOSH_FLAGS
# A radix literal stored in a variable has the same value as a bare literal.
# Default mood addition remains exact beyond the signed 64-bit range.
"$BIN" -c 'h=0xff; b=0b101; o=010; d=42; n=-0x10; echo $((h)) $((b)) $((o)) $((d)) $((n))'
"$BIN" -c 'h=0xff; b=0b101; echo $((h + b)) $((h * 2))'
"$BIN" -c 'v=0x7fffffffffffffff; echo $((v + 1))'
echo "rc=$?"
"$BIN" --mood bash --enable-extended-arithmetic -c \
  'v=18446744073709551616; echo $((v + 1))'
echo "extended-rc=$?"

# A subscript taken from a value, an operand, or a nameref never runs a
# command substitution, and an associative key keeps it literally. A parameter
# reference in such a subscript expands as in Bash. Each blocked form must stop
# with a subscript diagnostic and a non-zero status, and the marker file that
# the substitution would create must stay absent. A control run shows that the
# same invocation creates the marker when the substitution is allowed.
marker=$TEST_TEMP_DIRECTORY/arith-value-substitution
errors=$TEST_TEMP_DIRECTORY/arith-value-errors
mkdir -p "$TEST_TEMP_DIRECTORY"
rm -f "$marker"
blocked() {
  rm -f "$marker"
  "$BIN" --mood "$2" -c "$3" shell "$marker" >/dev/null 2>"$errors"
  status=$?
  if [ -f "$marker" ]; then
    echo "$1: ran"
  elif [ "$status" -ne 0 ] && grep -q subscript "$errors"; then
    echo "$1: blocked"
  else
    echo "$1: stopped without a subscript diagnostic, status=$status"
  fi
}
for mood in bash kosh; do
  rm -f "$marker"
  "$BIN" --mood "$mood" -c ': "$(: > "$1")"' shell "$marker" >/dev/null 2>&1
  if [ -f "$marker" ]; then echo "control $mood: creates the marker"; else echo "control $mood: marker missing"; fi
  rm -f "$marker"
done
for form in '[[ $x -lt 1 ]]' 'declare -i n; n=$x' 'RANDOM=$x' 'y=$(( x ))'; do
  blocked "value $form" bash "x='a[\$(: > \$1)]'; $form"
done
"$BIN" --mood bash -c \
  'declare -A m; k="\$(: > $1)"; m[$k]=7; echo $(( m[$k] + 1 ))
   declare -n r=m; (( r[$k] += 2 )); echo "${m[$k]}"' \
  shell "$marker" 2>&1
if [ -f "$marker" ]; then echo "key substitution ran"; else echo "key substitution blocked"; fi
"$BIN" --mood bash -c 'a=(1 2 3); i=1; q="a[\$i]"
  declare -A m=([x]=9); k=x; j="m[\$k]"; echo "$((q)) $((j))"' 2>&1
# A ${name@P} prompt expansion in such a subscript also counts, because it runs
# the substitutions held in the value.
blocked "prompt arithmetic" bash \
  'a=(1 2); k="\$(: > $1)"; q="a[\${k@P}]"; echo $((q))'
blocked "prompt unset" kosh \
  'a=(x y); k="\$(: > $1)"; s="\${k@P}"; unset "a[$s]"'
# An indirection or a function substitution in such a subscript cannot run a
# command either, while an indirection to a plain value still works.
blocked "indirect reference" bash \
  'a=(1 2); r="a[\$(: > $1)]"; q="a[\${!r}]"; echo $((q))'
blocked "function substitution arithmetic" bash \
  'a=(1 2); q="a[\${ : > $1; }]"; echo $((q))'
blocked "function substitution unset" kosh \
  'a=(x y); s="\${ : > $1; }"; unset "a[$s]"'
"$BIN" --mood bash -c 'a=(1 2); r=i; i=1; q="a[\${!r}]"; echo "indirect=$((q))"' 2>&1

# A builtin operand or a nameref target expands its subscript again.
# Parameter and arithmetic expansion still work there, and the kosh mood
# rejects a command substitution taken from a value.
for form in 'unset "a[$k]"' 'printf -v "a[$k]" x' '[[ -v "a[$k]" ]]' \
  'declare "a[$k]=1"' 'declare -n r="a[$k]"; echo "$r"' \
  'declare -n r="a[$k]"; r=5'; do
  blocked "operand $form" kosh "a=(x y); k='\$(: > \$1)'; $form"
done
"$BIN" --mood kosh -c '
a=(x y z); i=1
unset "a[\$i]"
printf -v "a[\$i]" %s new
unset "a[\$((1+1))]"
declare -n r="a[\$i]"; echo "r=$r"
declare -p a' 2>&1
