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

"$BIN" --debug-highlight-at 'echo $(true # comment
inner-command) outer' </dev/null |
    grep -E '^\(span "(# comment" :role comment|inner-command" :role unknown-command)\)$'

"$BIN" \
    --debug-highlight-at 'echo $(( 1 + $(printf ")") + 2 )); echo after' \
    </dev/null |
    grep -E ' :role (resolved-command|string)\)$'

"$BIN" --debug-highlight-at 'echo $(case x in x) probecmd a;; esac)' \
    -c 'probecmd() { :; }' </dev/null |
    grep -E '^\(span "(esac" :role keyword|probecmd" :role resolved-command)\)$'
