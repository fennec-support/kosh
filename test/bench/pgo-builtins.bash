#!/bin/bash
# Trains substitutions, pipelines into koshkit utilities, here-strings, external
# commands, and a glob over the directory in the first argument.

i=0
while [ "$i" -lt 2000 ]; do
    line=$(printf "%d\n" "$i" | koshkit cat)
    env true
    { read -r field; } <<<"$line"
    : "$(echo "$field")" "$(( field * 3 ))"
    i=$((i + 1))
done
printf "%s\n" a b c | while read -r word; do koshkit tr a-z A-Z <<<"$word"; done
for file in "$1"/*; do [[ -e $file ]] && : "${file##*/}"; done
