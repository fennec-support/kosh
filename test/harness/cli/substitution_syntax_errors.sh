dir=$(mktemp -d) || exit 1
trap 'cd / && [ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
cd "$dir" || exit 1

check()
{
  local name=$1
  printf '%s\n' "== $name"
  "$BIN" --lint "$name" 2>&1
  printf 'status=%s\n' "$?"
}

printf '#!/bin/bash\n\ndiff <(echo one) <(echo two; produce-two --now <S-D-.><><>)\n' \
  > process.sh
printf 'x=$(echo <><>)\necho after\n' > command.sh
printf 'echo before\ny=`echo <><>`\n' > backtick.sh
printf 'echo "quoted $(echo "inner $(echo <><>)")"\n' > quoted.sh
printf 'cat <<EOF\nline\nvalue: $(echo <><>)\nEOF\n' > heredoc.sh
printf 'cat <<-EOF\n\tvalue: $(echo\n\t  <><>)\n\tEOF\n' > heredoc_strip.sh
printf 'echo ${x:-$(echo <><>)}\n' > modifier.sh
printf 'echo $(( $(echo <><>) + 1 ))\n' > arithmetic.sh
printf 'x=${ echo <><>; }\n' > funsub.sh
printf 'echo $(if true; then echo; )\n' > unterminated_if.sh
printf 'x=$(echo <><>)\necho ok\ny=`echo <><>`\nif true; then\n  z=$(if)\nfi\n' \
  > several_commands.sh
printf 'echo ok\nvalues[$(echo <><>)]=1\n' > subscript.sh
printf 'declare -A map\nmap[$(echo <><>)]+=1\nexport map[`echo <><>`]=2\n' \
  > subscript_forms.sh
printf '(( values[$(echo <><>)]++ ))\n' > subscript_arithmetic.sh
printf 'echo "${x:-'"'"'$(echo <><>)'"'"'}"\n' > quoted_modifier.sh
printf 'echo $(echo <><>) "$(echo ok)" $(echo <><>) <(echo <><>)\n' \
  > several_words.sh
printf 'cat <<EOF\n$(echo <><>)\n$(echo <><>)\nEOF\necho $(echo <><>)\n' \
  > several_heredoc.sh
printf 'echo $(echo $(echo <><>) $(echo <><>))\n' > several_nested.sh

nest()
{
  local depth=$1 body='echo ok' level=0
  while [ "$level" -lt "$depth" ]; do
    body="echo \$($body)"
    level=$((level + 1))
  done
  printf '%s\n' "$body"
}
nest 64 > depth_limit.sh
nest 65 > depth_over.sh
{
  printf 'x=$(< /etc/hostname)\n'
  printf 'y=$(case a in a) echo;; esac)\n'
  printf 'cat <<EOF\n\\$(echo <><>) $(echo ok)\nEOF\n'
  printf "cat <<'EOF'\n"
  printf '$(echo <><>)\nEOF\n'
  printf 'z=`echo \\`echo ok\\``\n'
  printf 'w=${x:-"$(echo ok)"}\n'
  printf "v=\"\${x:-'\$(echo ok)'}\"\n"
  printf 'values[$(echo 1)]=2\n'
  printf "echo '\$(echo <><>)' \${x:-'a\"\$(echo <><>)\"'}\n"
} > valid.sh

check process.sh
check command.sh
check backtick.sh
check quoted.sh
check heredoc.sh
check heredoc_strip.sh
check modifier.sh
check arithmetic.sh
check funsub.sh
check unterminated_if.sh
check subscript.sh
check subscript_forms.sh
check subscript_arithmetic.sh
check quoted_modifier.sh
check several_commands.sh
check several_words.sh
check several_heredoc.sh
check several_nested.sh
printf '%s\n' '== depth_limit.sh'
"$BIN" --lint depth_limit.sh 2>&1 | grep -c 'nested too deeply'
printf '%s\n' '== depth_over.sh'
"$BIN" --lint depth_over.sh 2>&1 | grep -c 'nested too deeply'
printf '%s\n' '== valid.sh'
"$BIN" --lint valid.sh 2>&1 | grep -c 'Expected a filename'

printf '%s\n' '== run command.sh'
"$BIN" command.sh 2>&1
printf 'status=%s\n' "$?"

printf '%s\n' '== run process.sh'
"$BIN" process.sh 2>&1
printf 'status=%s\n' "$?"

printf '%s\n' '== run backtick.sh'
"$BIN" backtick.sh 2>&1
printf 'status=%s\n' "$?"

printf '%s\n' '== run -c'
"$BIN" -c 'echo before; echo $(echo <><>)' 2>&1
printf 'status=%s\n' "$?"
