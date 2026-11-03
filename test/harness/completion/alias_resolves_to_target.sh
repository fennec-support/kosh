echo "== aliased subcommands:"
"$BIN" --debug-complete-at 'g check-ref' \
    -c 'complete -W "check-ref-format --version" target; alias g=target; shopt -s progcomp_alias' </dev/null
echo "== aliased options:"
"$BIN" --debug-complete-at 'g --vers' \
    -c 'complete -W "check-ref-format --version" target; alias g=target; shopt -s progcomp_alias' </dev/null
echo "== an alias of koshkit completes utility words:"
"$BIN" --debug-complete-at 'kit mk' -c 'alias kit=koshkit' </dev/null
echo "== an alias of koshkit completes the flags of its utility:"
"$BIN" --debug-complete-at 'kit du --t' -c 'alias kit=koshkit' </dev/null
echo "== an alias naming a utility completes its flags after a separator:"
"$BIN" --debug-complete-at 'true; kdu --t' -c "alias kdu='koshkit du -s'" </dev/null
echo "== a koshkit utility offers no flag of another utility:"
"$BIN" --debug-complete-at 'koshkit du --wi' </dev/null | wc -l | tr -d ' '
echo "== an alias of a builtin completes the builtin flags:"
"$BIN" --debug-complete-at 'dc -' -c 'alias dc=declare' </dev/null |
  grep -c -e '^-[a-zA-Z]$'
