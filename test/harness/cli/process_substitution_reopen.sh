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
