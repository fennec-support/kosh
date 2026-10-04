unset KOSH_FLAGS
echo "== brace body =="
"$BIN" -c 'c() { echo $((1/0)); }; c' 2>&1
printf 'rc=%s\n' "$?"
echo "== function keyword =="
"$BIN" -c 'function d { echo $((1/0)); }; d' 2>&1
printf 'rc=%s\n' "$?"
echo "== subshell body =="
"$BIN" -c 'e() ( echo $((1/0)) ); e' 2>&1
printf 'rc=%s\n' "$?"
echo "== prefixed definition and trailing text =="
"$BIN" -c ':; g() { :; echo $((1/0)); } ; g' 2>&1
printf 'rc=%s\n' "$?"
echo "== multiline body =="
"$BIN" -c 'f() {
  echo $((1/0))
}
f' 2>&1
printf 'rc=%s\n' "$?"
