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

    SCALE=20 "$kosh" --mood sh --no-diagnostics "$bench/configure.sh" >/dev/null
    SCALE=20 "$kosh" --mood bash --no-diagnostics "$bench/configure.bash" \
        >/dev/null
    SCALE=5 "$kosh" --mood bash -W "$bench/configure.bash" >/dev/null 2>&1
    SCALE=10 "$kosh" "$bench/configure.kosh" >/dev/null
    "$kosh" --mood bash --no-diagnostics "$bench/primes.bash" 50000 >/dev/null

    SCALE=20 "$kosh" --mood sh "$bench/generate-script.sh" >"$work/script.bash"
    "$kosh" --lint "$work/script.bash" 2>/dev/null
    "$kosh" --format "$work/script.bash" >/dev/null 2>&1
    "$kosh" --lint --format "$work/script.bash" >/dev/null 2>&1

    "$kosh" -c 'koshkit seq 200000 >"$1"
    koshkit sort -r "$1" | koshkit sort -n | koshkit uniq | koshkit wc -l
    koshkit grep -E "1(2|3)+$" "$1" | koshkit cut -c1-3 | koshkit tr 1 2
    koshkit sed -n "s/7/8/p" "$1" | koshkit tail -n 1
    koshkit cat "$1" "$1" | koshkit wc -c' kosh "$work/lines" >/dev/null
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
