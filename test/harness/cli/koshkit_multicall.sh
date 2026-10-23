# A binary reached through a koshkit utility name acts as that utility, the
# busybox multicall. The shell binary is symlinked to ls in a temporary
# directory, and running that symlink lists the directory.
unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
d=$(mktemp -d) || exit 1
cd "$d" || exit 1

"$BIN" -c 'koshkit seq 1 > one.txt'
"$BIN" -c 'koshkit seq 1 > two.txt'
ln -s "$BIN" ls

echo "--- binary invoked as ls acts as ls ---"
./ls
echo "rc=$?"

ln -s "$BIN" tail

echo "--- unknown flag on a symlinked utility reports an error, not a crash ---"
./tail --bogus
echo "rc=$?"

echo "--- a symlinked utility reports the kosh version on --version ---"
./tail --version | grep -c "Koshka Shell"

echo "--- a symlinked utility opens its help with the bundled banner ---"
[ "$(./tail --help | head -1)" != DESCRIPTION ] && echo banner-first
./tail --help | grep -c '^DESCRIPTION$'

echo "--- the koshkit form opens with the description instead ---"
"$BIN" -c 'koshkit tail --help' | head -1

ln -s "$BIN" sleep

echo "--- a symlinked utility locates an invalid operand ---"
./sleep invalid 2>&1

ln -s "$BIN" tabs

echo "--- a specialized utility locates an invalid operand ---"
./tabs 4,nope 2>&1

ln -s "$BIN" koshkit

echo "--- a binary invoked as koshkit runs the utility its first operand selects ---"
./koshkit seq 2
echo "rc=$?"

echo "--- a binary invoked as koshkit passes the rest to the utility ---"
./koshkit tail --bogus 2>&1 | head -1
echo "--- a binary invoked as koshkit without a utility acts as the builtin ---"
./koshkit --list | grep -c '^seq$'
./koshkit no-such-utility > /dev/null 2>&1
echo "rc=$?"
echo "--- the builtin form skips the startup and cites koshkit in an error ---"
mkdir -p conf/kosh
printf 'bogus.option=on\n' > conf/kosh/kosh.conf
XDG_CONFIG_HOME="$d/conf" KOSH_FLAGS=-x ./koshkit no-such-utility 2>&1
echo "rc=$?"
XDG_CONFIG_HOME="$d/conf" KOSH_FLAGS=-x ./koshkit --list 2>&1 | grep -c '^seq$'
./koshkit --color bogus seq 1 2>&1
echo "rc=$?"

unset KOSH_FLAGS
# assimilate without a target installs kosh into a local directory with a
# symlink for each utility, the busybox-style install. A symlinked invocation
# routes its own flags to the utility rather than the shell CLI. The directory
# lives in the runner's work directory, which the runner removes.
dir=$TEST_MKTEMP_DIRECTORY/multicall-assimilate
provider=$TEST_MKTEMP_DIRECTORY/multicall-provider
mkdir -p "$dir" "$provider" || exit 1
ln -sf "$(command -v uname)" "$provider/uname"
PATH="$dir:$provider" "$BIN" -c 'assimilate -y --install-directory "$1"' _ \
  "$dir" </dev/null >/dev/null 2>&1
echo "== the kosh binary was installed:"
[ -x "$dir/kosh" ] && [ ! -L "$dir/kosh" ] && echo kosh-installed
echo "== a utility that PATH already provides is not linked:"
[ -L "$dir/uname" ] && echo uname-shadowed || echo uname-kept
echo "== the egrep alias link runs grep -E:"
printf 'axb\nab\n' | "$dir/egrep" 'a(x|y)b'
echo "== a symlink was installed for head:"
[ -L "$dir/head" ] && echo head-linked || echo head-missing
echo "== a symlinked head routes its own -c flag to the utility:"
printf 'abcdefgh' > "$dir/sample"
"$dir/head" -c 3 "$dir/sample"
echo ""
echo "== a symlinked ls routes -A to ls (count of the dot file):"
: > "$dir/.dotfile"
"$dir/ls" -A -1 "$dir" | grep -c '^\.dotfile$'
