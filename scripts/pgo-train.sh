#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Runs the training workloads for a profile-guided release build with the
# instrumented kosh: the benchmark scripts, script analysis, and the bundled
# text utilities. Generated input stays in WORK_DIRECTORY.
#
# Usage: pgo-train.sh KOSH WORK_DIRECTORY

set -eu

if [ $# -ne 2 ]; then
    echo "Usage: $0 KOSH WORK_DIRECTORY" >&2
    exit 2
fi

kosh=$1
work=$2
root=$(cd "$(dirname "$0")/.." && pwd)
bench=$root/test/bench

mkdir -p "$work"

SCALE=40 "$kosh" "$bench/configure.sh" >/dev/null
"$kosh" -M bash "$bench/configure.bash" >/dev/null
"$kosh" "$bench/configure.kosh" >/dev/null
"$kosh" "$bench/primes.bash" 100000 >/dev/null
"$kosh" -c 'true'

for script in "$bench/configure.sh" "$bench/configure.bash"; do
    "$kosh" --lint "$script" >/dev/null 2>&1 || :
    "$kosh" --format "$script" >/dev/null 2>&1 || :
done

"$kosh" -c 'i=0; while [ "$i" -lt 200000 ]; do
    printf "%d line%d field%d %d\n" "$i" "$((i % 97))" "$((i % 13))" \
        "$(( (i * 7919) % 100003 ))"
    i=$((i + 1))
done' > "$work/lines.txt"

for options in '' '-n' '-k4,4n' '-k2,2 -k3,3n' '-f' '-r' '-u' '-k4'; do
    # shellcheck disable=SC2086
    "$kosh" -c "koshkit sort $options \"\$1\"" kosh "$work/lines.txt" >/dev/null
done

"$kosh" -c 'koshkit grep -c line1 "$1"; koshkit cut -d" " -f2,4 "$1" | koshkit uniq -c | koshkit wc -l; koshkit tr a-z A-Z < "$1" | koshkit head -n 1000 >/dev/null; koshkit sed -n "s/line/row/p" "$1" | koshkit tail -n 5' kosh "$work/lines.txt" >/dev/null
