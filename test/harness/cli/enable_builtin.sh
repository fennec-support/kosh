unset KOSH_FLAGS KOSHCONF
# enable removes a builtin from command lookup until it is enabled again,
# toggles a bundled utility in a mood where it is otherwise unreachable, and
# lists the disabled commands. Changes in a subshell do not reach its parent.
work=$(mktemp -d)
trap 'cd / && [ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
mkdir "$work/bin"
printf '#!/bin/sh\nprintf "program echo %%s\\n" "$*"\n' >"$work/bin/echo"
chmod +x "$work/bin/echo"

do_kosh() {
  env -i ASAN_OPTIONS="${ASAN_OPTIONS-}" PATH="$work/bin" HOME="$work" XDG_CONFIG_HOME="$work/config" \
    "$BIN" "$@" 2>&1 | grep -v -e '^ ' -e '^note:'
}

echo "== a disabled builtin yields to PATH:"
do_kosh --mood bash -c 'enable -n echo; echo hi; type -t echo
builtin echo direct; printf "builtin status=%s\n" "$?"
enable -n; compgen -A disabled
(type -t echo; enable echo; type -t echo); type -t echo
enable echo; echo back; type -t echo'

echo "== each builtin word is a separate entry:"
do_kosh --mood bash -c 'enable -n test; [ x ] && echo "[ stays"
test x; printf "test status=%s\n" "$?"
compgen -A enabled te; compgen -A disabled te; enable test; compgen -A enabled te'

echo "== -s lists only special builtins:"
do_kosh --mood bash -c 'listed=; while read -r line; do
  listed="$listed ${line#enable }"
done < <(enable -s)
for name in export trap source echo cd; do
  case "$listed " in *" $name "*) echo "$name listed";; *) echo "$name omitted";; esac
done'

echo "== listings are sorted and -a marks disabled entries:"
do_kosh --mood bash -c 'enable -n test; enable -a | { read -r first; read -r second; read -r third
  printf "%s|%s|%s\n" "$first" "$second" "$third"; }
enable -a | while read -r line; do [ "$line" = "enable -n test" ] && echo "$line"; done
enable -n cd; compgen -A disabled
enable -p echo | { read -r first; echo "$first"; }'

echo "== loadable builtins are not supported:"
do_kosh --mood bash -c 'enable -d echo nosuch; printf "delete status=%s\n" "$?"
enable -f "$HOME/missing.so" x; printf "load status=%s\n" "$?"'

echo "== a disabled builtin expands redirections as a program:"
do_kosh --mood bash -c 'echo hi >${a:=/dev/null}; printf "builtin a=%s\n" "$a"
enable -n echo; echo hi >${b:=/dev/null}; printf "program b=%s\n" "$b"
command echo hi >${c:=/dev/null}; printf "command c=%s\n" "$c"'

echo "== a disabled special builtin is an ordinary command:"
do_kosh --mood sh -c 'enable -n export; export 1x; echo "the shell continues"'

echo "== a bundled utility is enabled and disabled by name:"
do_kosh --mood bash -c 'basename a/b; printf "status=%s\n" "$?"
enable basename; basename a/b; type -t basename; command -v basename
enable -n basename; basename a/b; enable -n
koshkit basename a/b'

echo "== the kosh mood disables a reachable utility:"
do_kosh -c 'basename a/b; enable -n basename; command -v basename
printf "status=%s\n" "$?"'

echo "== an unknown name fails:"
do_kosh -c 'enable nosuchbuiltin; printf "status=%s\n" "$?"'
