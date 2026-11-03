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

# Command substitutions, subshells, pipeline stages, and process
# substitutions keep i in $-.
probe --norc -ic 'echo "$(echo "$-")" "`echo "$-"`"; (echo "$-")
echo "$-" | cat; cat <(echo "$-")'

# A shell that takes commands from standard input sets s in $-, with or
# without -i and -s, and keeps it in subshells.
printf '%s\n' 'echo "stdin $- $(case $- in *s*) echo subshell-s;; esac)"' \
  > "$d/stdin.sh"
for flags in -i '-i -s' -s ''; do
  HOME=$d "$d/bash" --norc $flags < "$d/stdin.sh" 2>/dev/null
  echo "status=$?"
done

# -i over standard input that is not a terminal takes one complete command at
# a time. It prints PS1 and PS2 on stderr, expands and records history, and
# prints exit at the end of input. Koshka keeps its history in
# KOSH_HISTORY_FILE, and Bash keeps it in ~/.bash_history.
mkdir "$d/lines"
export KOSH_HISTORY_FILE=$d/lines/.kosh_history
printf '%s\n' 'echo one' 'echo !!' 'if true; then' '  echo two' 'fi' 'false' \
  'history 2' > "$d/lines.sh"
HOME=$d/lines PS1='P1> ' PS2='P2> ' "$d/bash" --norc +o emacs -i \
  < "$d/lines.sh" 2>&1 | grep -v -e 'job control' -e 'process group'
echo "status=${PIPESTATUS[0]}"
grep -c . "$d/lines/".*_history

# The shell keeps running after expansion, arithmetic, and syntax errors,
# leaves the rest of standard input to the commands it runs, and exits with
# the status of the last command.
printf '%s\n' ': "${unset_var?boom}"' 'echo after-expansion-error' \
  'echo $((1 / 0))' 'echo after-arithmetic-error' 'fi' \
  'echo after-syntax-error' 'echo "two' 'lines"' 'read -r line' 'read-me' \
  'echo "line=$line"' '(exit 5)' > "$d/errors.sh"
HOME=$d/lines "$d/bash" --norc -i < "$d/errors.sh" 2>/dev/null
echo "status=$?"

# A script operand of -i runs the same way without prompts, and a syntax
# error ends it with status 2.
printf '%s\n' 'echo "script $-"' 'echo !!' ': "${unset_var?boom}"' \
  'echo after-error' 'fi' 'echo never' > "$d/script-errors.sh"
HOME=$d/lines "$d/bash" --norc -i "$d/script-errors.sh" 2>/dev/null
echo "status=$?"

cd / && rm -rf "$d"
