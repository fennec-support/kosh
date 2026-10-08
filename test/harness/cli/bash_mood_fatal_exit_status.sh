unset KOSH_FLAGS
start=$PWD
dir=$(mktemp -d)
trap 'cd "$start" && rm -rf "$dir"' EXIT
cd "$dir"
printf 'set -u\necho "$undefined_variable"\necho after\n' > setu.sh
printf 'echo "${missing:?is gone}"\necho after\n' > report.sh
echo "== a bash-mood set -u abort exits 1 like a bash script, not 127 =="
"$BIN" --mood bash setu.sh
echo "rc=$?"
echo "== a bash-mood \${name:?} abort exits 1 like a bash script =="
"$BIN" --mood bash report.sh
echo "rc=$?"
trap_line='trap '\''echo "trap saw $?"'\'' EXIT'
printf '%s\n: "${CFG?must be set}"\necho after\n' "$trap_line" > trap_report.sh
printf '%s\nset -u\necho "$undefined_variable"\necho after\n' "$trap_line" \
  > trap_setu.sh
printf '%s\nx=0\necho $((1/x))\necho after\n' "$trap_line" > trap_arith.sh
echo "== a -c string ends 127 in the bash moods on a fatal expansion error =="
for mood in kosh bash bash-posix sh; do
  for body in 'echo ${x:?m}; echo after' 'set -u; echo $u; echo after' \
    'x=a; echo ${x@Z}; echo after' 'echo $((1/0)); echo after' \
    'f() { echo ${x:?m}; }; f; echo after' 'set -e; echo ${x:?m}; echo after' \
    '(echo ${x:?m}); echo "sub rc=$?"' \
    "trap 'echo trap saw \$?' EXIT; echo \${x:?m}"; do
    "$BIN" --mood "$mood" -c "$body" 2>/dev/null
    echo "$mood [$body] rc=$?"
  done
done
echo "== a script file and standard input keep status 1 =="
printf 'echo ${x:?m}; echo after\n' > file_fatal.sh
"$BIN" --mood bash file_fatal.sh 2>/dev/null
echo "file rc=$?"
"$BIN" --mood bash < file_fatal.sh 2>/dev/null
echo "stdin rc=$?"
echo "== the EXIT trap reads the status a fatal expansion error ends with =="
for mood in kosh bash bash-posix sh; do
  for script in trap_report.sh trap_setu.sh trap_arith.sh; do
    "$BIN" --mood "$mood" "$script" 2>/dev/null
    echo "$mood $script rc=$?"
  done
done
echo "== a background simple command of a -c string ends 127, a subshell or group 1 =="
for mood in bash bash-posix; do
  for body in '(: ${x:?m}) & wait $!; echo $?' \
    '{ : ${x:?m}; } & wait $!; echo $?' ': ${x:?m} & wait $!; echo $?' \
    'set -u; : $y & wait $!; echo $?' 'set -u; (: $y) & wait $!; echo $?' \
    'x=a; : ${x@Z} & wait $!; echo $?' 'v=${y:?m} & wait $!; echo $?' \
    'f() { : ${x:?m}; }; f & wait $!; echo $?' \
    '{ : ${x:?m} & wait $!; echo $?; }' \
    '(: ${x:?m} & wait $!; echo $?)' 'set -e; : ${x:?m} & wait $!; echo $?'; do
    "$BIN" --mood "$mood" -c "$body" 2>/dev/null
    echo "$mood [$body]"
  done
done
echo "== a background command of a script file keeps status 1 =="
printf ': ${x:?m} & wait $!; echo $?\n' > file_async.sh
"$BIN" --mood bash file_async.sh 2>/dev/null
"$BIN" --mood bash < file_async.sh 2>/dev/null
