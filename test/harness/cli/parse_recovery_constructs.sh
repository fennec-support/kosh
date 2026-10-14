unset KOSH_FLAGS KOSHCONF
# A parse error inside a compound command skips the rest of that command, so
# its then, fi, done, and closing brace report nothing. Recovery counts only
# keywords and braces in command position and ignores case patterns and heredoc
# bodies. A later error is reported once the failed command closes, or once a
# line starts at or left of the indentation of the failed command.

do_check() {
  echo "== $1"
  "$BIN" --mood sh --no-config -c "$2" 2>&1 | grep -E '^-c:[0-9]+:[0-9]+: error'
}

do_check "a failed if inside a function reports once" 'f() {
  if [[ $1 == x ]]; then
    echo yes
  else
    echo no
  fi
}'

do_check "nested C-style loops report the outer loop once" 'f() {
  for ((i = 1; i <= 3; i++)); do
    for ((j = 1; j <= 3; j++)); do
      echo "$i $j"
    done
  done
}'

do_check "an orphan after the failed command is reported" 'if [[ x ]]; then
  echo inside
fi
fi'

do_check "an error outside any command keeps every later error" '[[ x ]]
fi
done'

do_check "keywords and braces as arguments do not close a command" 'if [[ x ]]; then
  echo fi done esac } {
fi
done'

do_check "case patterns inside a failed command do not close it" 'if [[ x ]]; then
  case y in a) echo a;; (b) echo b;; esac
fi
fi'

do_check "a one-line failed command closes on its own keywords" 'if [[ x ]]; then case y in a) echo;; esac; fi
fi'

do_check "a heredoc body inside a failed command is skipped" 'if [[ x ]]; then
  cat <<EOF
fi
done
EOF
fi
fi'

do_check "an unterminated failed command reports once at the end" 'f() {
  if [[ x ]]; then
    echo open'

do_check "elif and else branches stay inside the failed command" 'if [[ x ]]; then
  :
elif [ y ]; then
  :
else
  :
fi
done'

do_check "each failed function reports its own error" 'f() {
  if [[ x ]]; then :; fi
}
g() {
  for ((i = 0; i < 2; i++)); do :; done
}'

do_check "a subshell and a brace group inside a failed command" 'while [[ x ]]; do
  ( cd / && { echo in; } )
done
}'

do_check "an unclosed failed command stops hiding errors at the next outer line" 'f() {
  if [[ x ]]; then
    echo a
}
g() {
  echo ok
  )
}'

do_check "a heredoc terminator at the left margin does not end the skip" 'f() {
  if [[ x ]]; then
    cat <<-EOF
EOF
    cat <<"END"
}
END
  fi
}
fi'

do_check "an unindented body reports its closer instead of hiding errors" 'if [[ x ]]; then
echo a
fi'

do_check "comments and blank lines at the margin keep the skip" 'f() {
  if [[ x ]]; then
# a comment at the margin

    echo a
  fi
}
fi'

do_check "tab indentation ends the skip at the next outer line" "$(printf 'f() {\n\tif [[ x ]]; then\n\t\techo a\n}\ng() {\n\t)\n}')"

do_check "a real error right after the failed command closes is reported" 'if [[ x ]]; then
  echo a
fi; done'

do_check "a failed command on one line leaves the next line alone" 'if [[ x ]]; then echo a; fi
echo b; esac'

do_check "deeply nested constructs report once" 'f() {
  while :; do
    case x in
      a)
        for i in 1; do
          { if [[ x ]]; then :; fi; }
        done
        ;;
    esac
  done
}
}'

echo "== a script file recovers the same way"
script_file=$(mktemp) || exit 1
trap '"$BIN_DIR/invoke-koshkit" rm -f -- "$script_file"' EXIT
printf 'f() {\n  if [[ x ]]; then\n    echo in\n  fi\n}\necho after\nfi\n' \
  >"$script_file"
"$BIN" --mood sh --no-config "$script_file" 2>&1 | grep -E ':[0-9]+:[0-9]+: error' |
  "$BIN_DIR/invoke-koshkit" sed 's/^.*:\([0-9]*:[0-9]*: error\)/\1/'

echo "== the bash mood recovers from its own errors"
"$BIN" --mood bash --no-config -c 'f() {
  for 1x in a; do
    echo "$1x"
  done
}
done' 2>&1 | grep -E '^-c:[0-9]+:[0-9]+: error'

do_check "a while and an until inside a failed command" 'f() {
  if [[ x ]]; then
    while :; do break; done
    until :; do break; done
  fi
}
esac'
