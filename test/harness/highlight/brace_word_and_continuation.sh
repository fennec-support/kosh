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

tab=$(printf '\t')

"$BIN" --debug-highlight-at 'echo $(true # comment
inner-command) outer' </dev/null |
    grep -E "^(# comment${tab}comment|inner-command${tab}unknown-command)$"

"$BIN" \
    --debug-highlight-at 'echo $(( 1 + $(printf ")") + 2 )); echo after' \
    </dev/null |
    grep -E "${tab}(resolved-command|string)$"

"$BIN" -c 'probecmd() { :; }' \
    --debug-highlight-at 'echo $(case x in x) probecmd a;; esac)' </dev/null |
    grep -E "^(esac${tab}keyword|probecmd${tab}resolved-command)$"
