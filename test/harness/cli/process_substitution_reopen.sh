unset KOSH_FLAGS

check()
{
  printf '%s\n' "== $1"
  "$BIN" --no-init-files -c "$2" 2>&1
  printf 'status=%s\n' "$?"
}

check 'read after an unread open' \
  'f() { : < "$1"; koshkit cat "$1"; }; f <(echo hello)'
check 'read after two unread opens' \
  'f() { : < "$1"; : < "$1"; koshkit cat "$1"; }; f <(echo hello)'
check 'write after an unwritten open' \
  'f() { : > "$1"; echo written > "$1"; }; f >(koshkit cat); echo after'
check 'unread input' \
  ': < <(echo unread); echo after'
check 'unwritten output' \
  ': > >(koshkit cat); echo after'
check 'empty input' \
  'koshkit cat <(true); echo after'
check 'empty input after an unread open' \
  'f() { : < "$1"; koshkit cat "$1"; }; f <(true); echo after'
check 'large input' \
  'koshkit cat <(koshkit seq 1 20000) | koshkit tail -n 1'
check 'read builtin' \
  'while read -r line; do echo "got $line"; done < <(printf "a\nb\n")'
check 'rapid unread opens' \
  'f() { for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18; do printf "%s " "$i"; : < "$1"; done; koshkit cat "$1"; }; f <(echo now)'
check 'rapid unwritten opens' \
  'f() { for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18; do printf "%s " "$i"; : > "$1"; done; echo last > "$1"; }; f >(koshkit cat); echo after'
check 'readers continue in turn' \
  'f() { read -r a < "$1"; read -r b < "$1"; echo "a=$a b=$b"; }; f <(printf "1\n2\n")'
check 'read after the end' \
  'f() { koshkit cat "$1"; echo "first=$?"; koshkit cat "$1"; echo "second=$?"; }; f <(echo once)'
check 'writers append in turn' \
  'f() { echo one > "$1"; echo two > "$1"; }; f >(koshkit cat); echo after'
