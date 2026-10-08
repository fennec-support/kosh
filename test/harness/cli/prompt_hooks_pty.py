#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives an interactive shell through a real PTY in the bash and kosh moods
# and checks the prompt hooks that frameworks such as bash-preexec, starship,
# and terminal integrations install. A prompt hook prints a numbered marker,
# and each line is typed only after the marker of the previous prompt. PS1
# and PS0 expand command and arithmetic substitutions, backquotes, and list
# operators on PROMPT_COMMAND, a DEBUG trap sees each command once, a
# preexec hook armed by the prompt hook sees the typed command, a RETURN trap
# runs, and a background job is reported before the next prompt. Every
# element of a PROMPT_COMMAND array runs in order with the status of the
# last command, and with its $_ in the bash mood, a precmd hook appended as an element arms a preexec
# DEBUG trap the way bash-preexec does, and a RETURN trap set by an element
# runs. Each check prints one stable PASS line.

import fcntl
import os
import pty
import select
import shutil
import signal
import struct
import sys
import tempfile
import termios
import time


WAIT_SECONDS = 10.0
MOODS = ("bash", "kosh")
READY_HOOK = "n=$((n + 1)); echo \"<ready-$n>\""
SETUP_LINES = (
    "n=0; PROMPT_COMMAND='%s'" % READY_HOOK,
    "PS1='[ps1 $(echo cs) `echo bq` ${PROMPT_COMMAND[*]:+set}"
    " ${#PROMPT_COMMAND[@]} $((6 * 7))]\\$ '",
    "PS0='<ps0 $(echo zero) $((1 + 1)) ${PROMPT_COMMAND:0:1}>\\n'",
)


class Session:
    def __init__(self, binary, directory, mood, arguments=None):
        if arguments is None:
            arguments = ["-M", mood, "--norc", "-i"]
        environment = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "HOME": directory,
            "XDG_CONFIG_HOME": os.path.join(directory, "config"),
            "KOSH_HISTORY_FILE": os.path.join(directory, "history"),
            "TERM": "xterm-256color",
            "LANG": "C.UTF-8",
        }
        self.raw = bytearray()
        self.prompt_count = 0
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(directory)
            os.execve(binary, [binary] + arguments, environment)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", 24, 100, 0, 0))
        self.is_closed = False

    def pump(self, seconds):
        ready, _, _ = select.select([self.fd], [], [], seconds)
        if not ready:
            return True
        try:
            chunk = os.read(self.fd, 65536)
        except OSError:
            return False
        if not chunk:
            return False
        self.raw.extend(chunk)
        return True

    def wait_until(self, is_ready):
        deadline = time.monotonic() + WAIT_SECONDS
        while time.monotonic() < deadline:
            if is_ready():
                return True
            if not self.pump(0.02):
                return is_ready()
        return is_ready()

    def has_text(self, text, mark=0):
        return text.encode() in self.raw[mark:]

    def wait_for_prompt(self):
        self.prompt_count += 1
        marker = "<ready-%d>" % self.prompt_count
        return self.wait_until(lambda: self.has_text(marker))

    def run(self, line):
        os.write(self.fd, line.encode() + b"\r")
        return self.wait_for_prompt()

    def close(self):
        if self.is_closed:
            return
        self.is_closed = True
        try:
            os.kill(self.pid, signal.SIGKILL)
        except OSError:
            pass
        os.waitpid(self.pid, 0)
        os.close(self.fd)


class Report:
    def __init__(self):
        self.is_ok = True

    def check(self, name, session, is_passed):
        if is_passed:
            print("%s PASS" % name)
            return True
        print("%s FAIL" % name)
        tail = bytes(session.raw[-1500:]).decode("utf-8", "replace")
        sys.stderr.write("%s:\n%s\n" % (name, tail))
        self.is_ok = False
        return False


def check_line(session, report, name, line, expected):
    mark = len(session.raw)
    is_answered = session.run(line)
    is_shown = session.wait_until(
        lambda: all(session.has_text(text, mark) for text in expected))
    return report.check(name, session, is_answered and is_shown)


def start_session(binary, directory, mood, report, setup_lines):
    session = Session(binary, directory, mood)
    is_ready = all(session.run(line) for line in setup_lines)
    if report.check(mood + "-prompt-hook", session, is_ready):
        return session
    session.close()
    return None


def run_mood(binary, directory, mood, report):
    session = start_session(binary, directory, mood, report, SETUP_LINES)
    if session is None:
        return
    try:
        check_line(session, report, mood + "-ps1-expansions", "true",
                   ["[ps1 cs bq set 1 42]"])
        check_line(session, report, mood + "-ps0-expansions",
                   "echo \"answer-$((6 * 7))\"",
                   ["<ps0 zero 2 n>", "answer-42"])
        session.run("mark() { echo '<mark>'; }; ps0_saved=$PS0")
        check_line(session, report, mood + "-ps0-function-substitution",
                   "PS0='<ps0 ${ mark; } ${| REPLY=reply; }>\\n'; true", [])
        check_line(session, report,
                   mood + "-ps0-function-substitution-runs", "echo after",
                   ["<ps0 <mark> reply>", "after"])
        session.run("PS0=$ps0_saved")
        check_line(session, report, mood + "-return-trap",
                   "f() { trap 'echo \"<return-$((1 + 1))>\"' RETURN; }; f;"
                   " trap - RETURN",
                   ["<return-2>"])
        session.run("debug_count=0; trap 'debug_count=$((debug_count + 1))'"
                    " DEBUG")
        check_line(session, report, mood + "-debug-trap",
                   "debug_count=0; echo \"<debug-${debug_count}>\"",
                   ["<debug-1>"])
        session.run("trap - DEBUG")

        session.run("is_ready=; preexec() { [ -n \"$is_ready\" ] || return 0;"
                    " is_ready=; echo \"<preexec:$BASH_COMMAND>\"; }")
        session.run("trap 'preexec' DEBUG;"
                    " PROMPT_COMMAND=\"$PROMPT_COMMAND; is_ready=1\"")
        check_line(session, report, mood + "-preexec-sees-the-command",
                   "echo \"<ran-$((2 + 3))>\"",
                   ["<preexec:echo \"<ran-$((2 + 3))>\">", "<ran-5>"])
        session.run("trap - DEBUG; PROMPT_COMMAND='%s'" % READY_HOOK)

        session.run("sleep 0.1 &")
        check_line(session, report, mood + "-job-report", "sleep 0.4",
                   ["Done"])

        session.run("m=0; PROMPT_COMMAND=('m=$((m + 1))' '%s')"
                    % READY_HOOK)
        check_line(session, report, mood + "-prompt-command-array-elements",
                   "echo \"<middle-$m>\"", ["<middle-1>"])
        last_word = " $_" if mood == "bash" else ""
        session.run("PROMPT_COMMAND=('echo \"<first-$?>\"; false'"
                    " 'echo \"<second-$?%s>\"' '' '%s')"
                    % (last_word, READY_HOOK))
        check_line(session, report,
                   mood + "-prompt-command-elements-keep-the-status",
                   "false last-word",
                   ["<first-1>", "<second-1%s>"
                    % last_word.replace("$_", "last-word")])

        session.run("bp_mode=; bp_precmd() { bp_status=$?;"
                    " echo \"<precmd-$bp_status>\"; bp_mode=on; }")
        session.run("bp_preexec() { [ -n \"$bp_mode\" ] || return 0;"
                    " bp_mode=; echo \"<preexec:$BASH_COMMAND>\"; }")
        session.run("trap 'bp_preexec' DEBUG; PROMPT_COMMAND=('%s');"
                    " PROMPT_COMMAND+=(bp_precmd)" % READY_HOOK)
        check_line(session, report,
                   mood + "-preexec-array-sees-the-command",
                   "echo \"<ran-$((3 + 4))>\"; (exit 3)",
                   ["<preexec:echo \"<ran-$((3 + 4))>\">", "<ran-7>",
                    "<precmd-3>"])
        session.run("trap - DEBUG")

        session.run("hook() { trap 'echo \"<hook-return>\"; trap - RETURN'"
                    " RETURN; }; PROMPT_COMMAND=('%s' hook)" % READY_HOOK)
        check_line(session, report,
                   mood + "-prompt-command-element-return-trap", "true",
                   ["<hook-return>"])
    finally:
        session.close()


def run_all(do_run_mood):
    if sys.platform != "linux":
        print("prompt hook PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)
    directory = tempfile.mkdtemp(prefix="kosh-prompt-hooks-pty-")
    report = Report()
    try:
        for mood in MOODS:
            do_run_mood(binary, directory, mood, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(run_all(run_mood))
