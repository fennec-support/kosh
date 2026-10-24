#!/bin/bash
# compgen -G lists every match of its pattern whatever word follows, so an
# unquoted glob that expands to several paths still prints its first match,
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

# A -X filter removes matches of its pattern, a leading ! keeps only the
# matches, a & stands for the completed word, and a backslash quotes a
# pattern character.
: >apple.c
: >apple.txt
compgen -f -X '*.txt' -- app
compgen -f -X '!*.txt' -- app
compgen -f -X '&*.txt' -- app
compgen -W 'a* ax' -X 'a\*' -- a
compgen -W 'a&x aax' -X '*\&*' -- a
compgen -W '!a ba' -X '\!*' -- ''
compgen -W 'a* a*foo' -X '&' -- 'a*'
