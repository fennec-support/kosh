unset KOSH_FLAGS
# A command that flock or nohup starts runs with no blocked signal, even when
# the shell itself started with SIGTERM blocked.
work=$(mktemp -d)
trap '[ -n "$work" ] && "$BIN_DIR/invoke-koshkit" rm -rf -- "$work"' EXIT
mkdir "$work/lock"
"$PYTHON" - "$BIN" "$work" <<'PYTHON'
import os
import signal
import sys

signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGTERM})
binary, work = sys.argv[1], sys.argv[2]
script = ('koshkit flock "$1/lock" grep SigBlk /proc/self/status\n'
          'koshkit nohup grep SigBlk /proc/self/status 2>/dev/null')
os.execv(binary, [binary, "-c", script, "kosh", work])
PYTHON
