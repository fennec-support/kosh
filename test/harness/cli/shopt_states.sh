unset KOSH_FLAGS
# An unknown shopt name is an error or a silent non-zero under -q, hash -R
# rebuilds the PATH index, and autocd changes the directory only in an
# interactive shell.
echo "== an unknown name is an error:"; "$BIN" -c 'shopt bogusopt'; echo "rc=$?"
echo "== an unknown name under -q is silent:"; "$BIN" -c 'shopt -q bogusopt'; echo "rc=$?"
rehash_first=$TEST_TEMP_DIRECTORY/rehash-first
rehash_second=$TEST_TEMP_DIRECTORY/rehash-second
mkdir -p "$rehash_first" "$rehash_second"
printf '#!/bin/sh\nprintf "rehash first\\n"\n' > "$rehash_first/rehash-probe"
printf '#!/bin/sh\nprintf "rehash second\\n"\n' > "$rehash_second/rehash-probe.off"
chmod +x "$rehash_first/rehash-probe" "$rehash_second/rehash-probe.off"
echo "== hash -R rebuilds the PATH index:"
"$BIN" --mood bash --no-init-files --no-diagnostics -c '
PATH=$1:$2
rehash-probe
koshkit mv "$1/rehash-probe" "$1/rehash-probe.off"
koshkit mv "$2/rehash-probe.off" "$2/rehash-probe"
hash -R
rehash-probe
' kosh "$rehash_first" "$rehash_second"
autocd_root=$TEST_TEMP_DIRECTORY/autocd
autocd_bin=$TEST_TEMP_DIRECTORY/autocd-bin
mkdir -p "$autocd_root/one" "$autocd_root/two" "$autocd_root/-P" \
  "$autocd_root/tree/one/two/three/four" "$autocd_root/literal/..." \
  "$autocd_bin"
autocd_root=$(cd "$autocd_root" && pwd)
autocd_bin=$(cd "$autocd_bin" && pwd)
printf '#!/bin/sh\nprintf "dot-program\\n"\n' > "$autocd_bin/..."
chmod +x "$autocd_bin/..."
echo "== autocd changes directories in an interactive shell:"
autocd_env=$autocd_bin/interactive.sh
cat >"$autocd_env" <<'EOF'
shopt -q autocd
printf "default=%s\n" "$?"
"$AUTOCD_ROOT/one"
printf "absolute=%s\n" "${PWD##*/}"
..
if [ "$PWD" = "$AUTOCD_ROOT" ]; then
  echo parent=yes
else
  echo parent=no
fi
two
printf "relative=%s\n" "${PWD##*/}"
cd "$AUTOCD_ROOT"
./-P
printf "option-name=%s\n" "${PWD##*/}"
cd "$AUTOCD_ROOT/tree/one/two/three/four"
..
printf "dot-one=%s\n" "${PWD##*/}"
cd "$AUTOCD_ROOT/tree/one/two/three/four"
...
printf "dot-two=%s\n" "${PWD##*/}"
cd "$AUTOCD_ROOT/tree/one/two/three/four"
....
printf "dot-three=%s\n" "${PWD##*/}"
cd "$AUTOCD_ROOT/tree/one/two/three/four"
................................
printf "dot-root=%s\n" "$PWD"
cd "$AUTOCD_ROOT/tree/one/two/three/four"
PATH="$AUTOCD_BIN:$PATH"
...
printf "dot-precedence=%s\n" "$?"
cd "$AUTOCD_ROOT/literal"
./...
printf "literal=%s\n" "${PWD##*/}"
cd "$AUTOCD_ROOT/tree/one/two/three/four"
PATH=/usr/bin:/bin
shopt -u autocd
if "$AUTOCD_ROOT/one" >/dev/null 2>&1; then
  echo disabled=no
else
  echo disabled=yes
fi
if .. >/dev/null 2>&1; then
  echo dot-disabled=accepted
else
  echo dot-disabled=rejected
fi
shopt -s autocd
if "$AUTOCD_ROOT/one" extra >/dev/null 2>&1; then
  echo extra-operand=accepted
else
  echo extra-operand=rejected
fi
if ... extra >/dev/null 2>&1; then
  echo dot-extra=accepted
else
  echo dot-extra=rejected
fi
"$AUTOCD_ROOT/one"
printf "reenabled=%s\n" "${PWD##*/}"
EOF
AUTOCD_ROOT="$autocd_root" AUTOCD_BIN="$autocd_bin" ENV="$autocd_env" \
  "$BIN" --no-diagnostics -L sh -i </dev/null 2>/dev/null
echo "== autocd is enabled in the Bash mood by default but not in a script:"
AUTOCD_ROOT="$autocd_root" "$BIN" --no-init-files --no-diagnostics \
  --mood bash -c '
shopt -q autocd
printf "bash-default=%s\n" "$?"
cd "$AUTOCD_ROOT"
two 2>/dev/null
printf "bash-directory=%s status=%s\n" "${PWD##*/}" "$?"
cd "$AUTOCD_ROOT/tree/one/two/three/four"
... 2>/dev/null
printf "bash-dot-two=%s\n" "${PWD##*/}"
'
printf 'two 2>/dev/null\nprintf "script-directory=%%s status=%%s\\n" "${PWD##*/}" "$?"\n' \
  >"$autocd_bin/autocd-script.sh"
(cd "$autocd_root" && "$BIN" --no-init-files --no-diagnostics \
  "$autocd_bin/autocd-script.sh")
