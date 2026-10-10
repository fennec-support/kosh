#!/bin/bash
# compgen -G lists every match of its pattern whatever word follows, so an
# unquoted glob that expands to several names still prints its first match,
# while -X and -P still apply to the matches.
cd "$(mktemp -d)" || exit 1
mkdir Applications Desktop
: >zfile
compgen -G *
echo "unquoted=$?"
compgen -G 'A*' Z
echo "other-word=$?"
compgen -G '*' -X 'D*' Ap
echo "excluded=$?"
compgen -G '*' -P x- Ap
echo "prefixed=$?"
compgen -G 'nothing*' n
echo "no-match=$?"
