#!/bin/bash
# -i with -c or a script operand runs an interactive shell over that input, the
# way bash does. The shell sources ~/.bashrc first, sets i and H in $-, sets
# PS1, expands aliases, leaves history references alone, and exits with the
# status of the input. A fatal expansion error in the command string ends it
# with status 1 instead of 127. Every probe starts the shell through a link
# named bash, so Koshka selects the bash mood, and runs it under a scratch HOME
# whose ~/.bashrc prints a marker. Standard input is not a terminal, so job
# control stays off and no line editor starts. The interactive shells complain
# about job control on stderr, and the probes discard it.
d=$(mktemp -d)
ln -s "$BASH" "$d/bash"
printf 'echo bashrc-ran\n' > "$d/.bashrc"
printf '%s\n' 'echo "script $0 $1 $-"' 'alias hi="echo alias-ok"' 'hi' \
  'echo "${PS1:+ps1-set}"' 'exit 4' > "$d/script.sh"
cd "$d" || exit 1

probe() {
  HOME=$d "$d/bash" "$@" </dev/null 2>/dev/null
  echo "status=$?"
}

probe -ic 'echo "$-"; alias x="echo aliased"
x; echo "${PS1:+ps1-set}"'
probe -i -c 'exit 3'
probe -ci 'echo "$0 $1 $-"' zero one
probe -i script.sh one
probe --norc -ic 'echo "$-"'
probe --norc -ic 'echo one; echo !!'
probe --norc -ic 'set -u; echo "$nope"; echo after'
probe --norc -ic 'false'

cd / && rm -rf "$d"
