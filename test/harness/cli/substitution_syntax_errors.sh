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
{
  printf 'x=$(< /etc/hostname)\n'
  printf 'y=$(case a in a) echo;; esac)\n'
  printf 'cat <<EOF\n\\$(echo <><>) $(echo ok)\nEOF\n'
  printf "cat <<'EOF'\n"
  printf '$(echo <><>)\nEOF\n'
  printf 'z=`echo \\`echo ok\\``\n'
  printf 'w=${x:-"$(echo ok)"}\n'
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
