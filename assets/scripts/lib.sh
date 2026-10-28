#!/usr/bin/bash

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Shared helpers for the demo recordings. A recording runs kosh under asciinema
# inside a private tmux server and types into it with send-keys. The scratch
# environment that setup.sh prepares lives in KOSH_DEMO_DIRECTORY.

DEMO_DIRECTORY=${KOSH_DEMO_DIRECTORY:-${TMPDIR:-/tmp}/kosh-demo}
DEMO_HOME=$DEMO_DIRECTORY/home
DEMO_SOCKET=kosh-demo-$$
DEMO_ASCIINEMA=${ASCIINEMA:-asciinema}

mkdir -p "$DEMO_DIRECTORY/casts"

T()
{
  tmux -L "$DEMO_SOCKET" "$@"
}

# start NAME DIRECTORY: records kosh started in DIRECTORY into casts/NAME.cast
# with a fresh copy of the demo history file.
start()
{
  local name=$1 directory=$2

  cp "$DEMO_DIRECTORY/history" "$DEMO_DIRECTORY/history-$name"
  T -f "$DEMO_DIRECTORY/tmux.conf" new-session -d -x 115 -y 30 -c "$directory" \
    "KOSH_HISTORY_FILE='$DEMO_DIRECTORY/history-$name' $DEMO_ASCIINEMA rec -q --overwrite -i 2 -c '$DEMO_DIRECTORY/env.sh kosh' '$DEMO_DIRECTORY/casts/$name.cast'"
  wait_for '•' 10
  sleep 1.2
}

# type_text TEXT [DELAY]: types TEXT one character at a time.
type_text()
{
  local text=$1 delay=${2:-0.07} index

  for ((index = 0; index < ${#text}; index++)); do
    T send-keys -l -- "${text:index:1}"
    sleep "$delay"
  done
}

key()
{
  T send-keys "$@"
}

# keys_slow DELAY KEY...: sends each key with DELAY between them.
keys_slow()
{
  local delay=$1 name

  shift
  for name in "$@"; do
    T send-keys "$name"
    sleep "$delay"
  done
}

# wait_for TEXT [SECONDS]: waits until the pane shows TEXT.
wait_for()
{
  local text=$1 limit=${2:-20} step

  for ((step = 0; step < limit * 10; step++)); do
    T capture-pane -p 2>/dev/null | grep -qF -- "$text" && return 0
    sleep 0.1
  done
  echo "timed out waiting for: $text" >&2
  T capture-pane -p >&2

  return 1
}

# finish: waits for the recorded shell to exit and the session to close.
finish()
{
  local step

  for ((step = 0; step < 300; step++)); do
    T has-session 2>/dev/null || return 0
    sleep 0.1
  done
  echo "the recorded session did not exit" >&2
  T kill-server

  return 1
}
