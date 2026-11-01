#!/bin/sh
# Diagnostics go to /dev/null, so the cases compare statuses and resulting files.

dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
cd "$dir" || exit 1

echo "--- rm -r with a trailing slash leaves the link target ---"
mkdir target holder
echo kept > target/file
ln -s ../target holder/link
"$BIN" -c 'koshkit rm -r holder/link/' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat target/file'

echo "--- mv and cp refuse a second source with the same name ---"
mkdir first second moved copied
echo 1 > first/x
echo 2 > second/x
"$BIN" -c 'koshkit cp first/x second/x copied' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat copied/x'
"$BIN" -c 'koshkit mv first/x second/x moved' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat moved/x second/x'

echo "--- a failed source leaves the next one free ---"
mkdir valid into
echo 3 > valid/y
"$BIN" -c 'koshkit mv missing/y valid/y into' 2>/dev/null
echo "status $?"
"$BIN" -c 'koshkit cat into/y'

echo "--- cp -r merges directories with one final component ---"
mkdir -p left/same right/same merged
echo 1 > left/same/one
echo 2 > right/same/two
"$BIN" -c 'koshkit cp -r left/same right/same merged'
echo "status $?"
"$BIN" -c 'koshkit cat merged/same/one merged/same/two'

echo "--- cut -d= takes the equals sign ---"
printf 'a=b\n' | "$BIN" -c 'koshkit cut -d= -f1'

echo "--- sed closes an unterminated line before more output ---"
printf 'a' | "$BIN" -c 'koshkit sed p'
echo
printf 'a\nb' | "$BIN" -c 'koshkit sed -e p -e "\$a\\
x"'
printf 'a' > unterminated
"$BIN" -c 'koshkit sed -i p unterminated'
"$BIN" -c 'koshkit wc -c unterminated'

echo "--- nesting ends in a diagnostic ---"
awk 'BEGIN { for (i = 0; i < 30000; i++) printf "f() "; print "true" }' > functions.sh
"$BIN" -n functions.sh 2>/dev/null
echo "status $?"
awk 'BEGIN { for (i = 0; i < 30000; i++) printf "coproc "; print "true" }' > coprocs.sh
"$BIN" -n coprocs.sh 2>/dev/null
echo "status $?"
awk 'BEGIN { printf "a=(1); echo $(( "; for (i = 0; i < 10000; i++) printf "a["; printf "0"; for (i = 0; i < 10000; i++) printf "]"; print " ))" }' > subscripts.sh
"$BIN" subscripts.sh 2>/dev/null
echo "status $?"

echo "--- bc and make stop deep recursion ---"
echo 'define f(n){if(n==0)return 0; return f(n-1)}; f(20000)' |
  "$BIN" -c 'koshkit bc' 2>/dev/null
echo "status $?"
echo 'define f(n){if(n==0)return 0; return f(n-1)}; f(1000)' |
  "$BIN" -c 'koshkit bc'
awk 'BEGIN { for (i = 1; i <= 10000; i++) print "t" i ": t" i + 1; print "t10001:;@echo end" }' > deep.mk
"$BIN" -c 'koshkit make -f deep.mk t1' 2>/dev/null
echo "status $?"

echo "--- mapfile refuses an origin with no room ---"
printf 'a\nb\n' |
  "$BIN" --mood bash -c 'mapfile -t -O 9223372036854775807 lines' 2>/dev/null
echo "status $?"

echo "--- fg in a forked stage ends with a diagnostic ---"
timeout 10 "$BIN" --mood bash -c 'sleep 0 & fg %1 | true; echo after' \
  2>/dev/null | tail -n 1
timeout 10 "$BIN" --mood bash -c 'sleep 1 & fg %1 | true; echo after' \
  2>/dev/null | tail -n 1

echo "--- koshconf refuses a configuration that is not a file ---"
mkdir -p config/kosh
mkfifo config/kosh/kosh.conf
XDG_CONFIG_HOME=$PWD/config HOME=$PWD timeout 10 "$BIN" \
  -c 'koshconf set editor.history.max_entries 5 --persist' 2>/dev/null
echo "status $?"

echo "--- --format keeps quoted text inside substitutions ---"
cat > quoted.sh <<'EOF'
echo "$(echo "a   b")"
echo "$(echo "a|b;c&&d")"
echo "${unset_name:-"a   b"}"
if [ "$(echo "a   b")" = "a   b" ]; then echo equal; else echo differ; fi
echo "$( (echo subshell) )"
EOF
"$BIN" --format quoted.sh > formatted.sh
"$BIN" --mood bash --no-diagnostics formatted.sh
"$BIN" --format formatted.sh | cmp - formatted.sh
echo "status $?"
