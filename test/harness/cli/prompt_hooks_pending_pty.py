#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# The prompt hook checks that Kosh does not yet pass, kept apart from
# prompt_hooks_pty.py so the suite stays green. Every element of a
# PROMPT_COMMAND array must run before the prompt, as bash-preexec and
# terminal integrations expect when they add their hooks as separate
# elements. Each check prints one stable PASS line.

import sys

from prompt_hooks_pty import check_line, run_all, start_session


ARRAY_SETUP_LINES = (
    "n=0; m=0; PROMPT_COMMAND=('n=$((n + 1))' 'm=$((m + 1))'"
    " 'echo \"<ready-$n>\"')",
)


def run_mood(binary, directory, mood, report):
    session = start_session(binary, directory, mood, report,
                            ARRAY_SETUP_LINES)
    if session is None:
        return
    try:
        check_line(session, report, mood + "-prompt-command-array-elements",
                   "echo \"<middle-$m>\"", ["<middle-1>"])
    finally:
        session.close()


if __name__ == "__main__":
    sys.exit(run_all(run_mood))
