unset KOSH_FLAGS
# The koshkit grep utility accepts the POSIX pattern, selection, and output
# options. Every case runs against fixed files in a temporary directory and
# prints the standard output followed by the exit status, which GNU grep and
# BusyBox grep agree on for these inputs.
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1
printf 'apple\nBanana\ncherry pie\n\napple pie\nx.y\n' > a
printf 'apple\nzzz\n' > b
printf 'pa\napple\n' > pats
printf 'apple\n\n' > blankpat
printf 'zzz' > nonl
: > emptypats

g() {
  echo "--- grep $*"
  "$BIN" -c "koshkit grep $*" 2>/dev/null </dev/null
  echo "status=$?"
}

g -c apple a b
g -c -v apple a
g -cv apple a
g -l apple a b
g -lx apple a b
g -lc apple a b
g -ch apple a b
g -nc apple a
g -q apple a missing
g -qE pp a
g -q zzzz a missing
g -qc apple a
g -s apple missing a
g -s zzzz missing a
g -c apple missing a
g -l apple a b missing
g -E "'ap+le|ch'" a
g "'ap+le'" a
g -E "'^(app|ban)'" a
g -E "'p{2}'" a
g -E "'^ap+'" a
g -E "'('" a
g -F "'x.y'" a
g -F "'[x'" a
g -Fi "'X.Y'" a
g -Fx "'x.y'" a
g -x apple a
g -xv apple a
g -x -i APPLE a
g -x "'app.e'" a
g -x "'.*pie'" a
g -xE "'apple|cherry pie'" a
g -x "''" a
g -e apple -e "'x\.y'" a
g -e "'apple'\$'\n''zzz'" -c a b
g -ie APPLE a
g -e "''" -c a
g -F -e "''" -c a
g -x -F "''" -c a
g -v -e "''" -c a
g -f pats a
g -f blankpat -c a
g -f nonl -c a
g -f emptypats -c a
g -f emptypats -vc a
g -f missingpats a
g -e apple -f pats -c a
g -c -e zzz -e Banana a b
g apple a -n
g -E -F x a
g -s -c apple missing
g -c apple - b
g -l apple -

echo "--- stdin names"
"$BIN" -c 'printf "apple\nzzz\n" | koshkit grep -c apple'
"$BIN" -c 'printf "apple\nzzz\n" | koshkit grep -l apple'
"$BIN" -c 'printf "apple\nzzz\n" | koshkit grep -c apple - b'
"$BIN" -c 'printf "apple\nzzz\n" | koshkit grep -l apple - b'

echo "--- error output"
"$BIN" -c 'koshkit grep -c apple missing a 2>&1 | koshkit grep -c "No such file"'
"$BIN" -c 'koshkit grep -sc apple missing a 2>&1 | koshkit grep -c "No such file"'
exit 0
