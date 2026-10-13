unset KOSH_FLAGS KOSHCONF

echo "== newgrp is blocked:"
PATH=/usr/bin:/bin "$BIN" -r -c 'newgrp; echo "status=$?"' 2>&1

echo "== bench is blocked:"
PATH=/usr/bin:/bin "$BIN" -r -c 'bench --no-shell /bin/echo; echo "status=$?"' 2>&1

echo "== assimilate is blocked:"
PATH=/usr/bin:/bin "$BIN" -r -c 'assimilate root@example.com; echo "status=$?"' 2>&1

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf 'echo sourced\n' >"$work/payload"

echo "== source without a slash searches PATH:"
PATH="$work:/usr/bin:/bin" "$BIN" -r -c 'source payload; echo "status=$?"' 2>&1

echo "== dot with a slash is blocked:"
(cd "$work" && PATH=/usr/bin:/bin "$BIN" -r -c '. ./payload; echo "status=$?"' 2>&1)

echo "== rescue mode keeps the shell restricted:"
printf 'cd /\n' | (exec -a -kosh "$BIN" -r --bogus) 2>&1 |
  grep -c 'cd is forbidden in a restricted shell'
