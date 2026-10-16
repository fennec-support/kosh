#!/bin/sh
# The getopts builtin parses a clustered option string, reads an option argument
# into OPTARG, advances OPTIND, and reports the rest after a shift, checked
# against dash. A leading colon in the option string selects the silent mode.

set -- -a -b value -ab arg2 plain1 plain2

while getopts ab:c opt; do
    case "$opt" in
    a) echo "saw a" ;;
    b) echo "saw b with $OPTARG" ;;
    c) echo "saw c" ;;
    *) echo "other" ;;
    esac
done

shift $((OPTIND - 1))
echo "remaining=$* count=$#"

OPTIND=1
set -- -x -z -y
while getopts :xy flag; do
    case "$flag" in
    x) echo "flag x" ;;
    y) echo "flag y" ;;
    '?') echo "unknown $OPTARG" ;;
    esac
done
echo "second_done"
