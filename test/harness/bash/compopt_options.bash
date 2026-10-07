#!/bin/bash
compopt
echo "outside-completion=$?"
compopt -o nospace
echo "outside-completion-with-option=$?"
compopt foo
echo "no-spec=$?"

complete -o filenames -W x foo
compopt foo
echo "print=$?"
compopt -o nospace -o dirnames foo
echo "enable=$?"
complete -p foo
compopt +o filenames +onospace foo
echo "disable=$?"
complete -p foo
compopt -o nospace +o nospace -o nospace foo
echo "off-wins=$?"
complete -p foo
compopt -onosort -- foo
complete -p foo

compopt -o bogus foo
echo "bad-name=$?"
compopt +o bogus foo
echo "bad-name-off=$?"
compopt -o nospace -o bogus foo
echo "bad-name-after-good=$?"
complete -p foo
compopt -x foo
echo "bad-option=$?"
compopt -o
echo "missing-name=$?"

complete -W y bar
compopt -o default foo missing bar
echo "missing-among-names=$?"
complete -p foo
complete -p bar

compopt -D
echo "no-default-spec=$?"
compopt -E
echo "no-empty-spec=$?"
compopt -I
echo "no-initial-spec=$?"
complete -D -W d
complete -E -W e
complete -I -W i
compopt -o nospace -D foo
echo "default-before-name=$?"
complete -p -D
complete -p foo
compopt -o plusdirs -I -E
echo "empty-before-initial=$?"
complete -p -E
complete -p -I
compopt +o nospace +D
complete -p -D
compopt -I
