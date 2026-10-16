set -e

hint() {
  printf '%s => ' "$1"
  "$BIN" --debug-hint-at "$1"
}

help_hint() {
  printf '%s => ' "$1"
  "$BIN" --debug-hint-at "$1" |
    sed -E -e 's/^(\[builtin synopsis\]).*/\1/' -e 's/^(\[flag\] [^:]*):.*/\1/'
}

printf '== open constructs\n'
hint 'echo "abc'
hint "echo 'abc"
hint "echo \$'abc"
hint 'echo $(ls'
hint 'echo `ls'
hint 'echo ${x'
hint 'echo $((1 +'
hint 'cat <(ls'
hint 'a=(1 2'
hint '(echo hi'
hint '{ echo hi'
hint 'f() {'
hint '[[ a == b'
hint 'if true'
hint 'if (true)'
hint 'if true; then'
hint 'while true'
hint 'while true; do :'
hint 'for x in a b'
hint 'for x in a; do '
hint 'case x in'
hint 'echo $(if true; then'

printf '== operators and redirections\n'
hint 'ls |'
hint 'ls |&'
hint 'ls &&'
hint 'ls ||'
hint '| ls'
hint '&& ls'
hint 'echo a; ; echo b'
hint '{ :; ; }'
hint 'echo >'
hint 'cat <'
hint 'cat <<'

printf '== stray and misplaced words\n'
hint 'echo )'
hint 'echo ;;'
hint 'fi '
hint 'echo; done '
hint 'case x '
hint 'function '
hint 'for 1x in a; do'
hint 'if true; then echo fi'

printf '== empty bodies\n'
hint '( )'
hint '{ }'
hint 'if ( )'
hint 'f() ( )'
hint 'f() { }'
hint 'while true; do done '
hint 'if true; then fi'
hint 'if :; then :; else fi'

printf '== caret at the end of the erroneous word\n'
hint 'fi'
hint 'echo; esac'
hint 'case x'
hint 'function'
hint 'if'
hint 'while :; do done'

printf '== continued on enter\n'
help_hint 'echo \'
help_hint 'echo "abc\'

printf '== clean lines\n'
help_hint 'echo "abc" "$(ls)" "${HOME}"'
help_hint 'echo hi # "abc'
help_hint 'echo \"abc'
hint 'read x <<EOF'
hint 'if true; then echo; fi'
hint 'f() { :; }'

printf '== analysis findings of a paused line\n'
hint 'echo $x'
hint 'cat file | grep x'
hint 'x = 1'
hint 'zzunknown-command arg'
help_hint 'source ./zz-missing-file.sh'

printf '== alias, function, and segment under the caret\n'
"$BIN" -c "alias zzl='ls -a'" --debug-hint-at 'zzl ' | sed -E 's/( · [^ ]+) .*/\1/'
"$BIN" -c 'zzf() { echo hi; }' --debug-hint-at 'zzf '
help_hint 'echo hi | cat -n'
help_hint 'echo hi && cat -n'
