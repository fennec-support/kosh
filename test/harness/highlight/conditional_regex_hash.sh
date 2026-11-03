set -e

for source in '[[ x =~ (x)#y ]]' '[[ x =~ (a #y) ]]'; do
  result=$("$BIN" --debug-highlight-at "$source")
  if printf '%s\n' "$result" | grep -q ' :role comment)$'; then
    echo "a regular expression hash became a comment in $source"
    exit 1
  fi
  printf '%s\n' "$result" | grep -Fx '(span "]]" :role keyword)'
done

result=$("$BIN" --debug-highlight-at '[[ x =~ a ]] #y')
printf '%s\n' "$result" | grep -Fx '(span "#y" :role comment)'
