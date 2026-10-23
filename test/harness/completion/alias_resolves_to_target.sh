echo "== aliased subcommands:"
"$BIN" -c 'complete -W "check-ref-format --version" target; alias g=target; shopt -s progcomp_alias' \
    --debug-complete-at 'g check-ref' </dev/null
echo "== aliased options:"
"$BIN" -c 'complete -W "check-ref-format --version" target; alias g=target; shopt -s progcomp_alias' \
    --debug-complete-at 'g --vers' </dev/null
echo "== an alias of koshkit completes utility words:"
"$BIN" -c 'alias kit=koshkit' --debug-complete-at 'kit mk' </dev/null
echo "== an alias of koshkit completes the flags of its utility:"
"$BIN" -c 'alias kit=koshkit' --debug-complete-at 'kit du --t' </dev/null
echo "== an alias naming a utility completes its flags after a separator:"
"$BIN" -c "alias kdu='koshkit du -s'" --debug-complete-at 'true; kdu --t' </dev/null
echo "== a koshkit utility offers no flag of another utility:"
"$BIN" --debug-complete-at 'koshkit du --ma' </dev/null | wc -l | tr -d ' '
echo "== an alias of a builtin completes the builtin flags:"
"$BIN" -c 'alias dc=declare' --debug-complete-at 'dc -' </dev/null |
  grep -c -e '^-[a-zA-Z]$'
