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

echo "--- an indirect subscript from data runs no command ---"
"$BIN" --no-diagnostics -c \
  'a=(1); v="a[\$(touch marker)0]"; echo "${!v}"' 2>/dev/null
echo "status $?"
ls marker 2>/dev/null
echo "status $?"

echo "--- an indirect value is a name and not an expression ---"
"$BIN" --mood bash --no-diagnostics -c \
  'v="u:-\$(touch injected)"; echo "${!v-d}"' 2>/dev/null
echo "status $?"
ls injected 2>/dev/null
echo "status $?"

echo "--- export -p leaves out names from the environment that are not names ---"
env 'bad$(touch exported)=1' "$BIN" --mood bash --no-diagnostics \
  -c 'eval "$(export -p)"' 2>/dev/null
ls exported 2>/dev/null
echo "status $?"

echo "--- deep conditionals end in a diagnostic ---"
awk 'BEGIN { printf "[[ "; for (i = 0; i < 200000; i++) printf "("; printf " a "; for (i = 0; i < 200000; i++) printf ")"; print " ]]" }' > conditional.sh
"$BIN" --no-diagnostics conditional.sh 2>/dev/null
echo "status $?"

echo "--- a self reference through an element ends ---"
"$BIN" --mood bash --no-diagnostics \
  -c 'declare -n r="r[0]"; echo "[$r]"' 2>/dev/null
echo "status $?"

echo "--- loop appends follow the working directory ---"
mkdir into_a into_b
"$BIN" --no-diagnostics \
  -c 'for d in into_a into_b; do cd "$d"; echo "$d" >> out; cd ..; done'
"$BIN" -c 'koshkit cat into_a/out into_b/out'

echo "--- a loop needs a condition ---"
timeout 10 "$BIN" --no-diagnostics -c 'while do :; done' 2>/dev/null
echo "status $?"

echo "--- local - restores an explicit set -u ---"
"$BIN" -c 'f() { local -; set -u; }; f; [ -z "$unset_operand" ]; echo "after $?"'

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
cat >> quoted.sh <<'EOF'
if true; then
cat <<E
foo\
E
E
fi
EOF
"$BIN" --format quoted.sh > formatted.sh
"$BIN" --mood bash --no-diagnostics formatted.sh
"$BIN" --format formatted.sh | cmp - formatted.sh
echo "status $?"
