#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Starts an interactive bash mood shell through a real PTY with a startup
# chain like a common login setup: .bashrc sources a POSIX .profile, which
# sources a bash startup file with nounset saved and restored. The startup
# file installs terminal integration hooks the way shell integrations do,
# appending to PROMPT_COMMAND by its type and adding a preexec hook to PS0
# that reads the command from history. Each check prints one stable PASS
# line.

import os
import shutil
import sys
import tempfile

from prompt_hooks_pty import Report, Session, check_line


BASHRC = r"""n=0
PROMPT_COMMAND='n=$((n + 1)); echo "<ready-$n>"'
. "$HOME/.profile"
[[ $- != *i* ]] && return
if [[ $BASH_VERSION != *kosh* ]]; then
  alias ll='echo "<alias-ll>"'
else
  alias ll='echo "<alias-ll>"'
fi
alias lr='echo "<alias-lr>"'
"""

PROFILE = r"""#!/bin/sh
export STARTUP_TOOLS_DIR="$HOME/tools"
export PATH="$PATH:$STARTUP_TOOLS_DIR/bin"
if command -v no-such-startup-tool >/dev/null 2>&1; then
  STARTUP_TOOL="$(command -v no-such-startup-tool)"
elif [ -x /no/such/tool ]; then
  STARTUP_TOOL=/no/such/tool
fi
if [ -n "${STARTUP_TOOL:-}" ]; then
  eval "$("$STARTUP_TOOL" env)"
fi
test -f "$HOME/.missing-env" && . "$HOME/.missing-env"
case ${BASH_VERSION:-} in
  [4-9]*|[1-9][0-9]*)
    for STARTUP_FILE in \
      "$HOME/startup.bash" \
      "${UNSET_STARTUP_DIR:+$UNSET_STARTUP_DIR/startup.bash}"; do
      [ -r "$STARTUP_FILE" ] || continue
      STARTUP_HAD_NOUNSET=
      case $- in
        *u*) STARTUP_HAD_NOUNSET=1; set +u ;;
      esac
      if . "$STARTUP_FILE"; then :; else :; fi
      if [ -n "$STARTUP_HAD_NOUNSET" ]; then
        set -u
      fi
    done
    unset STARTUP_FILE STARTUP_HAD_NOUNSET
    ;;
esac
export PATH
"""

STARTUP = r"""if [[ "$-" != *i* ]]; then builtin return; fi
_startup_saved_ps1="$PS1"
PS1='\[\e]133;P;k=i\a\][chain]\$ \[\e]133;B\a\]'

__startup_precmd() {
  builtin local ret="$1"
  builtin printf '<precmd-%s>' "$ret"
}

__startup_preexec_hook() {
  builtin local cmd
  cmd=$(LC_ALL=C HISTTIMEFORMAT='' builtin history 1)
  cmd="${cmd#*[[:digit:]][* ] }"
  [[ -n "$cmd" ]] && builtin printf '<preexec:%s>' "${cmd//[[:cntrl:]]/}"
}

__startup_hook() {
  builtin local ret=$?
  __startup_precmd "$ret"
  if [[ "$PS0" != *"__startup_preexec_hook"* ]]; then
    PS0+='$(__startup_preexec_hook)'
  fi
}

if (( BASH_VERSINFO[0] > 4 || (BASH_VERSINFO[0] == 4 && BASH_VERSINFO[1] >= 4) )); then
  if [[ ";${PROMPT_COMMAND[*]:-};" != *";__startup_hook 2>/dev/null;"* ]]; then
    if [[ -z "${PROMPT_COMMAND[*]}" ]]; then
      PROMPT_COMMAND=("__startup_hook 2>/dev/null")
    elif [[ $(builtin declare -p PROMPT_COMMAND 2>/dev/null) == "declare -a "* ]]; then
      PROMPT_COMMAND+=("__startup_hook 2>/dev/null")
    else
      [[ "${PROMPT_COMMAND}" =~ (\;[[:space:]]*|$'\n')$ ]] || PROMPT_COMMAND+=";"
      PROMPT_COMMAND="__startup_hook 2>/dev/null;$PROMPT_COMMAND"
    fi
  fi
fi
"""

FILES = {".bashrc": BASHRC, ".profile": PROFILE, "startup.bash": STARTUP}


def run_checks(binary, directory, report):
    for name, text in FILES.items():
        with open(os.path.join(directory, name), "w") as stream:
            stream.write(text)
    session = Session(binary, directory, "bash", ["-M", "bash", "-i"])
    try:
        is_ready = session.wait_for_prompt()
        if not report.check("startup-chain-reaches-the-prompt", session,
                            is_ready):
            return
        check_line(session, report, "startup-chain-path",
                   "echo \"<path-${PATH##*/}>\"", ["<path-bin>"])
        report.check("startup-chain-prompt", session,
                     session.has_text("[chain]$ "))
        check_line(session, report, "startup-chain-precmd-status", "false",
                   ["<precmd-1>"])
        check_line(session, report, "startup-chain-preexec-reads-history",
                   "echo \"<ran-$((4 + 5))>\"",
                   ["<preexec:echo \"<ran-$((4 + 5))>\">", "<ran-9>"])
        check_line(session, report, "startup-chain-aliases", "ll; lr",
                   ["<alias-ll>", "<alias-lr>"])
        check_line(session, report, "startup-chain-nounset-restored",
                   "[[ $- == *u* ]] && echo '<nounset-on>'"
                   " || echo '<nounset-off>'", ["<nounset-off>"])
    finally:
        session.close()


def main():
    if sys.platform != "linux":
        print("startup chain PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)
    directory = tempfile.mkdtemp(prefix="kosh-startup-chain-pty-")
    report = Report()
    try:
        run_checks(binary, directory, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
