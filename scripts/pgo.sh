#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Trains pgo/kosh.profdata with an instrumented release build and then a
# context-sensitive instrumented build. The build argument also builds ./kosh
# with the result.
#
# Usage: pgo.sh [build]

set -eu

cd "$(dirname "$0")/.."
root=$PWD
work=$root/pgo/work
bench=$root/test/bench
profdata="./scripts/llvm-tool.sh llvm-profdata"
mode=${1-}
set --

# clang passes context-sensitive options only to ld64.lld on Darwin.
if [ "$(uname -s)" = Darwin ]; then
    brew=$(brew --prefix)
    profdata=$brew/opt/llvm/bin/llvm-profdata
    set -- "CXX=$brew/opt/llvm/bin/clang++" \
        "LD64_LLD=$brew/opt/lld/bin/ld64.lld"
fi

# shellcheck disable=SC2086
build() {
    ${MAKE:-make} ${JOBS-} -C src MODE=rel "$@"
}

train() (
    kosh=$root/$1
    LLVM_PROFILE_FILE=$root/pgo/$2/default_%m.profraw
    HOME=$work
    XDG_CONFIG_HOME=$work/.config
    export LLVM_PROFILE_FILE HOME XDG_CONFIG_HOME
    unset KOSHCONF
    mkdir -p "$work"

    SCALE=40 "$kosh" --mood sh --no-diagnostics "$bench/configure.sh" >/dev/null
    SCALE=10 "$kosh" --mood sh -W "$bench/configure.sh" >/dev/null 2>&1
    SCALE=40 "$kosh" --mood bash --no-diagnostics "$bench/configure.bash" \
        >/dev/null
    SCALE=10 "$kosh" --mood bash -W "$bench/configure.bash" >/dev/null 2>&1
    SCALE=20 "$kosh" "$bench/configure.kosh" >/dev/null
    "$kosh" --mood bash --no-diagnostics "$bench/primes.bash" 100000 >/dev/null
    "$kosh" --mood bash -W "$bench/primes.bash" 20000 >/dev/null 2>&1

    SCALE=20 "$kosh" --mood sh "$bench/generate-script.sh" >"$work/script.bash"
    "$kosh" --lint "$work/script.bash" 2>/dev/null
    "$kosh" --format "$work/script.bash" >/dev/null 2>&1
    "$kosh" --lint --format "$work/script.bash" >/dev/null 2>&1
    find "$root/test" "$root/scripts" "$root/completions" -type f \
        \( -name '*.sh' -o -name '*.bash' -o -name '*.kosh' \) \
        -exec "$kosh" --lint {} + >/dev/null 2>&1 || :

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

    "$kosh" -c 'i=0; while [ "$i" -lt 200000 ]; do
        printf "%d line%d field%d %d\n" "$i" "$((i % 97))" "$((i % 13))" \
            "$(( (i * 7919) % 100003 ))"
        i=$((i + 1))
    done' >"$work/lines"

    for flags in '' -n -k4,4n '-k2,2 -k3,3n' -f -r -u -k4; do
        "$kosh" -c "koshkit sort $flags \"\$1\"" kosh "$work/lines" >/dev/null &
    done
    wait

    "$kosh" -c 'koshkit grep -c line1 "$1"
    koshkit grep -E "field(1|7) [0-9]+5$" "$1" | koshkit wc -l
    koshkit cut -d" " -f2,4 "$1" | koshkit uniq -c | koshkit wc -l
    koshkit tr a-z A-Z < "$1" | koshkit wc -l
    koshkit sed -n "s/line/row/p" "$1" | koshkit tail -n 5
    koshkit cat "$1" | koshkit wc -c' kosh "$work/lines" >/dev/null
)

echo Training a release profile...
rm -rf pgo/profraw pgo/csprofraw
build "$@" PGO=generate kosh
train kosh-pgo-gen profraw
$profdata merge -o pgo/kosh-ir.profdata pgo/profraw/*.profraw
build "$@" PGO=cs-generate PGO_PROFILE="$root/pgo/kosh-ir.profdata" kosh
train kosh-pgo-csgen csprofraw
$profdata merge -o pgo/kosh.profdata pgo/kosh-ir.profdata \
    pgo/csprofraw/*.profraw

if [ "$mode" = build ]; then
    echo Building a profile-guided release...
    build "$@" PGO_PROFILE="$root/pgo/kosh.profdata" kosh
fi
