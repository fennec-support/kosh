set -e

show() {
  printf '== %s\n' "$1"
  "$BIN" --debug-highlight-at "$1"
}

show 'echo {a..e} "x y"'
show 'echo "$(echo {a..e})" "x y"'
show '{ echo a; } | cat'
show 'function g { echo; }'
show 'x=$(\
  A=1 \
    cat)'
show 'myfn() { :; }; echo "$(myfn)" `myfn`'
show 'myfn() { :; }; unset -f myfn other; unset -v myfn'
