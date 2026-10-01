set -e

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
