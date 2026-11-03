echo "== alias and function operands:"
"$BIN" --debug-complete-at 'type zz' \
    -c 'PATH=; alias zzalias=echo; zzfunc() { :; }' </dev/null
echo "== builtin operand:"
"$BIN" --debug-complete-at 'type unali' -c ':' </dev/null
echo "== keyword operand:"
"$BIN" --debug-complete-at 'type whil' -c ':' </dev/null
echo "== absent operand:"
"$BIN" --debug-complete-at 'type zzz-absent' -c ':' </dev/null
echo "== flags still answer:"
"$BIN" --debug-complete-at 'type -' -c ':' </dev/null
