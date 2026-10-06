#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

# Points the Arch and Alpine recipes in deploy/ at a release. With no operand
# it resolves the latest release. It pins the release commit, its commit time,
# the toiletline commit the release's submodule points to, and the SHA-512 of
# both source tarballs, so a recipe bump is one command.
#
# Run:
#     scripts/update-package-recipes.sh [TAG]

set -eu

REPOSITORY=https://github.com/fennec-support/kosh
TOILETLINE=https://github.com/toiletbril/toiletline
ROOT=$(cd "$(dirname "$0")/.." && pwd)
PKGBUILD=$ROOT/deploy/archlinux/PKGBUILD
APKBUILD=$ROOT/deploy/alpine/APKBUILD

fail()
{
    printf 'update-package-recipes: %s\n' "$1" >&2
    exit 1
}

fetch()
{
    curl --proto '=https' --tlsv1.2 --retry 3 -fsSL "$@"
}

sha512_of()
{
    if command -v sha512sum > /dev/null; then
        sha512sum "$1" | cut -d ' ' -f 1
    else
        shasum -a 512 "$1" | cut -d ' ' -f 1
    fi
}

recipe_value()
{
    sed -n "s/^$1=\"\{0,1\}\([^\"]*\)\"\{0,1\}\$/\1/p" "$2" | head -n 1
}

set_recipe_value()
{
    sed "s|^$1=.*|$1=$2|" "$3" > "$3.new"
    mv "$3.new" "$3"
}

replace_checksums()
{
    awk -v block="$2" '
        /^sha512sums=/ { print block; skipping = 1; next }
        skipping && /^["\)]$/ { skipping = 0; next }
        skipping { next }
        { print }
    ' "$1" > "$1.new"
    mv "$1.new" "$1"
}

command -v curl > /dev/null || fail "curl is required"
command -v git > /dev/null || fail "git is required"

VERSION=${1:-}
if [ -z "$VERSION" ]; then
    VERSION=$(fetch -o /dev/null -w '%{url_effective}' "$REPOSITORY/releases/latest")
    VERSION=${VERSION##*/}
fi
case $VERSION in
    '' | latest | releases | *[!A-Za-z0-9._-]*) fail "no release tag resolved" ;;
esac

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT INT TERM

git -C "$WORK" init -q source
git -C "$WORK/source" fetch -q --depth 1 "$REPOSITORY" "refs/tags/$VERSION" ||
    fail "the tag $VERSION does not exist"
RELEASE_COMMIT=$(git -C "$WORK/source" rev-parse 'FETCH_HEAD^{commit}')
RELEASE_EPOCH=$(git -C "$WORK/source" log -1 --format=%ct FETCH_HEAD)
TOILETLINE_COMMIT=$(git -C "$WORK/source" ls-tree FETCH_HEAD src/toiletline |
    cut -d ' ' -f 3 | cut -f 1)
[ -n "$TOILETLINE_COMMIT" ] || fail "the release has no toiletline submodule"

fetch -o "$WORK/kosh.tar.gz" "$REPOSITORY/archive/refs/tags/$VERSION.tar.gz"
fetch -o "$WORK/toiletline.tar.gz" "$TOILETLINE/archive/$TOILETLINE_COMMIT.tar.gz"
KOSH_SHA512=$(sha512_of "$WORK/kosh.tar.gz")
TOILETLINE_SHA512=$(sha512_of "$WORK/toiletline.tar.gz")

for RECIPE in "$PKGBUILD" "$APKBUILD"; do
    if [ "$(recipe_value pkgver "$RECIPE")" != "$VERSION" ]; then
        case $RECIPE in
            "$PKGBUILD") set_recipe_value pkgrel 1 "$RECIPE" ;;
            *) set_recipe_value pkgrel 0 "$RECIPE" ;;
        esac
    fi
    set_recipe_value pkgver "$VERSION" "$RECIPE"
    set_recipe_value _release_commit "\"$RELEASE_COMMIT\"" "$RECIPE"
    set_recipe_value _release_epoch "$RELEASE_EPOCH" "$RECIPE"
    set_recipe_value _toiletline_commit "\"$TOILETLINE_COMMIT\"" "$RECIPE"
done

replace_checksums "$PKGBUILD" "sha512sums=(
  '$KOSH_SHA512'
  '$TOILETLINE_SHA512'
)"
replace_checksums "$APKBUILD" "sha512sums=\"
$KOSH_SHA512  kosh-$VERSION.tar.gz
$TOILETLINE_SHA512  toiletline-$TOILETLINE_COMMIT.tar.gz
\""

printf 'Recipes now build %s (%s, toiletline %s).\n' \
    "$VERSION" "$RELEASE_COMMIT" "$TOILETLINE_COMMIT"
