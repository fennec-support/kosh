unset KOSH_FLAGS

echo "== export completes variable words without an equals suffix:"
completion=$(\
  KOSH_EXPORT_COMPLETION_UNIQUE=1 \
    "$BIN" --debug-complete-at 'export KOSH_EXPORT_COMPLETION_UNI' </dev/null
)
printf '%s\n' "$completion"
if printf '%s\n' "$completion" | grep -Fx \
  'KOSH_EXPORT_COMPLETION_UNIQUE' >/dev/null &&
  ! printf '%s\n' "$completion" | grep -F '=' >/dev/null
then
  echo "export-candidate=plain"
else
  echo "export-candidate=wrong"
fi

has_name()
{
  printf '%s\n' "$1" | grep -Fx 'KOSH_EXPORT_COMPLETION_UNIQUE' >/dev/null
}

echo "== an assignment value is not completed with variable words:"
completion=$(\
  KOSH_EXPORT_COMPLETION_UNIQUE=1 \
    "$BIN" --debug-complete-at 'export KOSH_EXPORT_COMPLETION_UNIQUE=' \
    </dev/null
)
if has_name "$completion"; then echo "export-value=variables"; else
  echo "export-value=none"
fi
completion=$(\
  KOSH_EXPORT_COMPLETION_UNIQUE=1 \
    "$BIN" --debug-complete-at \
    'export KOSH_EXPORT_COMPLETION_UNIQUE=KOSH_EXPORT_COMPLETION_UNI' \
    </dev/null
)
if has_name "$completion"; then echo "export-value-prefix=variables"; else
  echo "export-value-prefix=none"
fi

echo "== binding builtins complete variable words after their options:"
for line in 'export -n ' 'readonly ' 'readonly -a ' 'declare ' 'declare -x ' \
  'declare -p ' 'typeset ' 'typeset -r ' 'local ' 'local -i '; do
  completion=$(\
    KOSH_EXPORT_COMPLETION_UNIQUE=1 \
      "$BIN" --debug-complete-at "${line}KOSH_EXPORT_COMPLETION_UNI" </dev/null
  )
  if has_name "$completion" &&
    ! printf '%s\n' "$completion" | grep -F '=' >/dev/null
  then
    echo "${line% }=plain"
  else
    echo "${line% }=wrong"
  fi
done

echo "== function selecting options do not complete variable words:"
for line in 'declare -f ' 'declare -F ' 'declare -fx ' 'typeset -f ' \
  'typeset -F ' 'export -f ' 'readonly -f '; do
  completion=$(\
    KOSH_EXPORT_COMPLETION_UNIQUE=1 \
      "$BIN" --debug-complete-at "${line}KOSH_EXPORT_COMPLETION_UNI" </dev/null
  )
  if has_name "$completion"; then echo "${line% }=variables"; else
    echo "${line% }=none"
  fi
done

# A bare unset operand completes variables with no leading dollar, and unset -f
# completes the defined functions.
echo "== unset completes a variable name:"
"$BIN" -c 'completion_probe_var=1' --debug-complete-at 'unset completion_probe' </dev/null

echo "== unset completes a dynamic ANSI variable name:"
"$BIN" --debug-complete-at 'unset KOSH_ANSI_RE' </dev/null

echo "== unset -f completes a function name:"
"$BIN" -c 'completion_probe_fn() { :; }' --debug-complete-at 'unset -f completion_probe' </dev/null

echo "== plain unset does not offer a function name:"
"$BIN" -c 'completion_probe_fn() { :; }' --debug-complete-at 'unset completion_probe' </dev/null
