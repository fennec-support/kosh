#!/bin/sh

unset KOSH_FLAGS
BIN=$(CDPATH= cd -- "$(dirname -- "$BIN")" && pwd)/$(basename -- "$BIN")
dir=$(mktemp -d) || exit 1
start=$PWD
trap 'cd "$start" && [ -n "$dir" ] && rm -rf "$dir"' EXIT
cd "$dir" || exit 1

mkdir -p man1
printf 'NAME\n  \033]0;PWNED-TITLE\007\033[44mblue\033[2J page text\n' > man1/injected.1
# A redirected stream keeps the raw page bytes.
MANPATH="$dir" "$BIN" --no-diagnostics -c 'koshkit man injected' > raw.out
echo "status=$?"
cat raw.out
# A terminal sees every control byte in caret or replacement form.
KOSH_BIN="$BIN" MAN_DIR="$dir" python3 - <<'EOF'
import os, pty, select, subprocess, sys

m, s = pty.openpty()
p = subprocess.Popen([os.environ['KOSH_BIN'], '--no-diagnostics', '-c',
                      'koshkit man injected'], stdout=s,
                     stderr=subprocess.PIPE,
                     env=dict(os.environ, MANPATH=os.environ['MAN_DIR']))
os.close(s)
data = b''
while True:
    r, _, _ = select.select([m], [], [], 10)
    if not r:
        break
    try:
        chunk = os.read(m, 65536)
    except OSError:
        break
    if not chunk:
        break
    data += chunk
p.wait()

esc = b'\x1b'
assert esc + b']0;' not in data and b'PWNED-TITLE' in data, data
assert esc + b'[44m' not in data and esc + b'[2J' not in data, data
assert b'blue' in data, data
print('terminal page keeps controls inert')
EOF
echo "pty=$?"
