#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Prints the bash completion file with its generated block rebuilt from the
# utilities and flags that `koshkit --list-flags` of the given kosh reports.
#
# Usage: generate-completions.sh KOSH COMPLETION_FILE

set -eu

if [ $# -ne 2 ]; then
    echo "Usage: $0 KOSH COMPLETION_FILE" >&2
    exit 2
fi

kosh=$1
file=$2
begin_marker='# BEGIN GENERATED KOSHKIT FLAGS'
end_marker='# END GENERATED KOSHKIT FLAGS'

if ! grep -q -x -F "$begin_marker" "$file" ||
    ! grep -q -x -F "$end_marker" "$file"
then
    echo "$0: '$file' has no generated block" >&2
    exit 1
fi

listing=$("$kosh" -c 'koshkit --list-flags')

generate_block()
{
    printf '%s\n' "$listing" | awk '
        BEGIN { line = "_koshkit_utils=\"" }
        {
            word = $1
            if (length(line) + length(word) + 1 > 78) {
                print line " \\"
                line = word
            } else if (line ~ /"$/) {
                line = line word
            } else {
                line = line " " word
            }
        }
        END { print line "\"" }
    '
    printf '\n_koshkit_util_flags ()\n{\n  case $1 in\n'
    printf '%s\n' "$listing" | awk '
        NF > 1 {
            flags = $2
            for (i = 3; i <= NF; i++) flags = flags " " $i
            print "    " $1 ")"
            if (length("      printf \"%s\\n\" \"" flags "\"") > 80) {
                print "      printf \"%s\\n\" \\"
                print "        \"" flags "\""
            } else {
                print "      printf \"%s\\n\" \"" flags "\""
            }
            print "    ;;"
        }
    '
    printf '  esac\n}\n'
}

# awk reads the block from the environment, since -v would decode its
# backslashes.
block=$(generate_block)
export block

awk -v begin_marker="$begin_marker" -v end_marker="$end_marker" '
    $0 == begin_marker { print; print ENVIRON["block"]; is_skipping = 1; next }
    $0 == end_marker { is_skipping = 0 }
    !is_skipping { print }
' "$file"
