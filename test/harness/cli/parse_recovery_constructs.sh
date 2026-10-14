unset KOSH_FLAGS KOSHCONF
# A parse error skips only the failed statement, and the command list around
# it keeps parsing, so a body closes on its own keyword and later errors in
# the same body are still reported. A closer that an enclosing command owns
# ends an inner command that lacks its own closer. Dialect errors, such as [[
# in the sh mood, are reported while the command parses as usual. Recovery
# reads tokens, so layout, heredoc bodies, and quoted text do not affect it.

do_check() {
  echo "== $1"
  "$BIN" --mood sh --no-config -c "$2" 2>&1 | grep -E '^-c:[0-9]+:[0-9]+: error'
}

do_check "a dialect error inside a function reports once" 'f() {
  if [[ $1 == x ]]; then
    echo yes
  else
    echo no
  fi
}'

do_check "each dialect use is reported" 'f() {
  for ((i = 1; i <= 3; i++)); do
    for ((j = 1; j <= 3; j++)); do
      echo "$i $j"
    done
  done
  cat <<< x
}'

do_check "an orphan after a closed command is reported" 'if [[ x ]]; then
  echo inside
fi
fi'

do_check "an error outside any command keeps every later error" '[[ x ]]
fi
done'

do_check "every error inside one body is reported" 'if true; then
  echo a |;
  echo b ;;
  echo c &&
fi
fi'

do_check "an unindented body recovers the same way" 'f() {
if true; then
echo a |;
fi
echo b ;;
}
}'

do_check "a closer of the enclosing command ends an unclosed if" 'f() {
  if true; then
    echo a
}
g() {
  echo ok
  )
}'

do_check "a mismatched closer ends an unclosed loop" 'if true; then
  while true; do
    echo a
fi
done'

do_check "an unclosed case ends at the closing brace" 'f() {
  case x in
    a) echo a ;;
}
}'

do_check "a header error skips its command by matched closers" 'case x
a) { echo a; } ;;
esac
esac'

do_check "keywords and braces as arguments do not close a command" 'if true; then
  echo fi done esac } { ; echo |;
fi
done'

do_check "case patterns inside a skipped command do not close it" 'case x
  a) echo a;; (b) echo b;; esac
fi'

do_check "a heredoc body inside a failed statement is skipped" 'if true; then
  cat <<EOF |;
fi
done
EOF
fi
fi'

do_check "a quoted heredoc delimiter and a multi-line string" 'f() {
  cat <<"E F"
}
E F
  echo "multi
fi" |;
}
}'

do_check "an unterminated command at the end reports once" 'f() {
  if true; then
    echo open'

do_check "elif and else branches recover inside the if" 'if true; then
  :
elif echo |; then
  :
else
  echo ;;
fi
done'

do_check "each failed function reports its own error" 'f() {
  if true; then :; fi |;
}
g() {
  for i in a; do echo |; done
}'

do_check "a subshell and a brace group inside a body" 'while true; do
  ( cd / && { echo |; } )
done
}'

do_check "deeply nested constructs report only the real error" 'f() {
  while :; do
    case x in
      a)
        for i in 1; do
          { if true; then echo |; fi; }
        done
        ;;
    esac
  done
}
}'

do_check "a missing then reports the if instead of its fi" 'if true; echo; fi
while true; echo; done'

do_check "independent errors after a failed command are reported" 'f() {
  if then
  fi
  if then
  fi
  echo ;;
}
}'

do_check "a missing do inside a function ends at the brace" 'f() {
  while true
}
echo |;'

do_check "an empty body keeps the errors after it" 'if true; then fi
echo a
done
f() { }
esac'

do_check "nested commands ended by one foreign closer report the innermost" 'f() { if true; then while true; do echo; }
fi'

echo "== a keyword taken as an argument is pointed out at the end"
"$BIN" --mood sh --no-config -c 'if true; then echo fi' 2>&1 |
  grep -E '^-c:[0-9]+:[0-9]+: (error|details)'

echo "== a script file recovers the same way"
dir=$(mktemp -d) || exit 1
trap '"$BIN_DIR/invoke-koshkit" rm -rf -- "$dir"' EXIT
printf 'f() {\n  if true; then\n    echo |;\n  fi\n}\necho after\nfi\n' \
  >"$dir/script.sh"
"$BIN" --mood sh --no-config "$dir/script.sh" 2>&1 |
  grep -E ':[0-9]+:[0-9]+: error' |
  "$BIN_DIR/invoke-koshkit" sed 's/^.*:\([0-9]*:[0-9]*: error\)/\1/'

echo "== lint reports every error in a file"
printf 'f() {\n  echo a |;\n  echo b ;;\n}\ng() {\n  if true; then\n    echo c\n}\n' \
  >"$dir/lint.sh"
"$BIN" --no-config --lint "$dir/lint.sh" 2>&1 |
  grep -E ':[0-9]+:[0-9]+: error' |
  "$BIN_DIR/invoke-koshkit" sed 's/^.*:\([0-9]*:[0-9]*: error\)/\1/'

echo "== the bash mood keeps parsing after a bad loop variable"
"$BIN" --mood bash --no-config -c 'f() {
  for 1x in a; do
    echo |;
  done
}
done' 2>&1 | grep -E '^-c:[0-9]+:[0-9]+: error'

echo "== lint reports [[ ]] operators without an operand"
for test_text in '[[ a == ]]' '[[ -f ]]' '[[ a < ]]' '[[ a == b && -z ]]' \
  '[[ a =~ ]]' '[[ 1 -lt (2) ]]' '[[ == ]]' '[[ ! -d x ]]' '[[ ( a == b ) ]]'
do
  printf '#!/bin/bash\n%s\n' "$test_text" >"$dir/conditional.sh"
  printf '%s => ' "$test_text"
  "$BIN" --no-config --lint "$dir/conditional.sh" 2>&1 |
    grep -c 'conditional-missing-operand\|has no operand after it'
done

echo "== recovery stops after 100 errors and says so"
line_index=0
while [ "$line_index" -lt 120 ]; do
  echo 'echo |;'
  line_index=$((line_index + 1))
done >"$dir/many.sh"
"$BIN" --no-config --lint "$dir/many.sh" 2>&1 | grep -c ': error: '
"$BIN" --no-config --lint "$dir/many.sh" 2>&1 | grep -c 'Parsing stopped'

do_check "a stray arm terminator in a case is reported" 'case x in a) echo ;; ;; esac
fi'

echo "== a closer inside a heredoc or quotes is not taken for a swallowed one"
"$BIN" --mood bash --no-config -n -c 'cat <<EOF
fi ) }
EOF
if x; then echo "fi"' 2>&1 | grep -E '^-c:[0-9]+:[0-9]+: (error|details)|taken|expected'

echo "== a closer taken as an argument is pointed out"
"$BIN" --mood bash --no-config -n -c '{ echo }' 2>&1 |
  grep -E '^-c:[0-9]+:[0-9]+: (error|details)|taken|expected'
