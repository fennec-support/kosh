#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Runs the training workloads for a profile-guided release build with an
# instrumented kosh. The workloads repeat the benchmark
# invocations, lint and format generated and bundled scripts, spawn processes,
# and run the bundled text utilities in parallel.
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

mkdir -p "$work/home"
work=$(cd "$work" && pwd)
HOME=$work/home
XDG_CONFIG_HOME=$work/home/.config
export HOME XDG_CONFIG_HOME
unset KOSHCONF

train_sh_evaluation() {
    SCALE=40 "$kosh" --mood sh --no-diagnostics "$bench/configure.sh" >/dev/null
    SCALE=10 "$kosh" --mood sh -W "$bench/configure.sh" >/dev/null 2>&1
}

train_bash_evaluation() {
    SCALE=40 "$kosh" --mood bash --no-diagnostics "$bench/configure.bash" \
        >/dev/null
    SCALE=10 "$kosh" --mood bash -W "$bench/configure.bash" >/dev/null 2>&1
    SCALE=20 "$kosh" "$bench/configure.kosh" >/dev/null
}

train_primes() {
    "$kosh" --mood bash --no-diagnostics "$bench/primes.bash" 100000 >/dev/null
    "$kosh" --mood bash -W "$bench/primes.bash" 20000 >/dev/null 2>&1
}

train_lint_and_format() {
    SCALE=20 "$kosh" --mood sh "$bench/generate-script.sh" \
        >"$work/generated.bash"
    "$kosh" --lint "$work/generated.bash" 2>/dev/null || :
    "$kosh" --format "$work/generated.bash" >"$work/formatted.bash" 2>/dev/null
    "$kosh" --format "$work/formatted.bash" >/dev/null 2>&1
    "$kosh" --lint --format "$work/generated.bash" >/dev/null 2>&1 || :
    find "$root/test" "$root/scripts" "$root/completions" -type f \
        \( -name '*.sh' -o -name '*.bash' -o -name '*.kosh' \) \
        -exec "$kosh" --lint {} + >/dev/null 2>&1 || :
}

train_processes() {
    "$kosh" --mood bash -c 'i=0
    while [ "$i" -lt 2000 ]; do
        line=$(printf "%d\n" "$i" | koshkit cat)
        env true
        { read -r field; } <<<"$line"
        : "$(echo "$field")" "$(( field * 3 ))"
        i=$((i + 1))
    done
    printf "%s\n" a b c | while read -r word; do koshkit tr a-z A-Z <<<"$word"; done
    for file in "$1"/*; do [[ -e $file ]] && : "${file##*/}"; done' \
        kosh "$root/src" >/dev/null
}

train_utilities() {
    "$kosh" -c 'i=0; while [ "$i" -lt 200000 ]; do
        printf "%d line%d field%d %d\n" "$i" "$((i % 97))" "$((i % 13))" \
            "$(( (i * 7919) % 100003 ))"
        i=$((i + 1))
    done' >"$work/lines.txt"

    for options in '' '-n' '-k4,4n' '-k2,2 -k3,3n' '-f' '-r' '-u' '-k4'; do
        # shellcheck disable=SC2086
        "$kosh" -c "koshkit sort $options \"\$1\"" kosh "$work/lines.txt" \
            >/dev/null &
    done

    "$kosh" -c 'koshkit grep -c line1 "$1"
    koshkit grep -E "field(1|7) [0-9]+5$" "$1" | koshkit wc -l
    koshkit cut -d" " -f2,4 "$1" | koshkit uniq -c | koshkit wc -l
    koshkit tr a-z A-Z < "$1" | koshkit head -n 1000 >/dev/null
    koshkit sed -n "s/line/row/p" "$1" | koshkit tail -n 5
    koshkit cat "$1" | koshkit wc -c' kosh "$work/lines.txt" >/dev/null

    wait
}

pids=
for workload in train_sh_evaluation train_bash_evaluation train_primes \
    train_lint_and_format train_processes train_utilities; do
    "$workload" &
    pids="$pids $!"
done

status=0
for pid in $pids; do
    wait "$pid" || status=1
done

exit "$status"
