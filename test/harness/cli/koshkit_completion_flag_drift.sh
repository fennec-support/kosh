unset KOSH_FLAGS
# Every short and long flag a koshkit utility lists under its help options must
# appear in the flags the bash completion offers for that utility. A value
# suffix such as --ignore= is compared by its name. The bare help flag is
# supplied by the completion function itself. Only a missing flag is printed.
completion=${BIN%/*}/completions/kosh.bash
missing_count=0

for utility in $("$BIN" -c 'koshkit --list'); do
  offered=" $("$BIN" -M bash -c '. "$1"; _koshkit_util_flags "$2"' kosh \
    "$completion" "$utility" | tr ' ' '\n' | sed 's/=$//' | tr '\n' ' ') "
  flags=$("$BIN" -c "koshkit $utility --help" 2>&1 \
    | sed -n -E '/^(  -|      --)/p' \
    | sed -E 's/^(.{0,38}[^ ]) {2,}.*/\1/; s/\[?=.*$//; s/^ +//; s/, /\n/g')
  for flag in $flags; do
    if [ "$flag" = --help ]; then
      continue
    fi
    case $offered in
      *" $flag "*) ;;
      *)
        echo "$utility: missing $flag"
        missing_count=$((missing_count + 1))
        ;;
    esac
  done
done

if [ "$missing_count" -eq 0 ]; then
  echo "ok: every help flag is completed"
fi
