#!/bin/bash
# complete rejects an unknown -o option, an unknown option after a known one
# without registering anything, and an unknown -A action. A valid
# registration is the positive control.
complete -o bogus foo
echo "unknown-option=$?"
complete -p foo 2>/dev/null
echo "unknown-option-registered=$?"

complete -o default -o bogus bar
echo "unknown-after-known=$?"
complete -p bar 2>/dev/null
echo "unknown-after-known-registered=$?"

complete -A bogus baz
echo "unknown-action=$?"
complete -p baz 2>/dev/null
echo "unknown-action-registered=$?"

complete -o nospace -o default qux
echo "known=$?"
complete -p qux
