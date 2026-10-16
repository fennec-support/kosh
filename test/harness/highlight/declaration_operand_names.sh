set -e

result=$("$BIN" --debug-highlight-at \
    'V=1; unset V; unset NOPE; export SHIPPED; declare -a rows; readonly frozen=2; local scoped; typeset -i counted; unset -v V; echo $SHIPPED $rows; echo $U1; U1=1; echo $U1; U2=1 echo $U2; echo $uarr; uarr[0]=1; echo $uarr; echo $PATH')
tab=$(printf '\t')
printf '%s\n' "$result" |
    grep -E "${tab}(unset-variable|variable|assignment-name|flag)$"
