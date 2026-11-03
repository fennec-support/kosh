printf 'echo   format\n' | "$BIN" -A --format
printf 'echo lint\n' | "$BIN" -A --lint --no-traces
printf 'echo   combined\n' | "$BIN" -A --lint --format --no-traces
printf 'name=ok; echo "$name" | koshkit cat\n' | "$BIN" -AR -

root=$TEST_TEMP_DIRECTORY/ast-operations
mkdir -p "$root"
trap '[ -n "$root" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$root"' EXIT

printf 'echo apply\n' > "$root/lint.sh"
"$BIN" -A --lint --apply --no-traces "$root/lint.sh"
printf 'echo   both\n' > "$root/both.sh"
"$BIN" -A --lint --format --apply --no-traces "$root/both.sh"
"$BIN_DIR/invoke-koshkit" cat "$root/both.sh"

printf '# heading\n\ntext\n\n```sh\necho   embedded\n```\n' > "$root/plain.md"
"$BIN" -A --format "$root/plain.md"
"$BIN" -A --lint --no-traces "$root/plain.md"

{
  printf '%s\n' 'x=a y=b'
  printf '%s\n' '{ echo a; } &> out'
  printf '%s\n' "cat <<'EOF' 2>&3- |& tr a b"
  printf '%s\n' 'x' 'EOF'
  printf '%s\n' 'FOO=1 declare -r name=value'
  printf '%s\n' '[[ -f x && ! $y == z* ]]'
  printf '%s\n' 'case $x in a|b*) echo ab ;; *) : ;& esac'
  printf '%s\n' 'f() { exec 3>&- 4<>file; }'
} > "$root/shapes.sh"
"$BIN" -A --lint --no-traces "$root/shapes.sh"
printf '%s\n' 'x=$(echo a | tr a b)' 'koshkit cat <(echo b)' \
  'y=`printf "%s" q`' 'echo "$x$y"' > "$root/substitutions.sh"
"$BIN" -A --lint --no-traces "$root/substitutions.sh"
printf '%s\n' "echo a\\*'b'* ~/x \"\$HOME\"" > "$root/words.sh"
"$BIN" -R --lint --no-traces "$root/words.sh"
{
  printf '%s\n' 'echo "${x:-d}" "${a[1]#q}" "${u:-$(echo hi)}" "${#x}" "${!p*}"'
  printf '%s\n' 'echo "${a[@]:1:2}" "${x//a/b}" "${x@Q}" "${!a[@]}" "${@:-w}"'
  printf '%s\n' 'echo "${y:=${z:-`echo q`}}" "${x%%*.}" "${!r}" "${10}" "${x:1}"'
} > "$root/params.sh"
"$BIN" -R --lint --no-traces "$root/params.sh"
