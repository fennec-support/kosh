#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives an interactive shell through a real PTY in every mood, with job
# control on and off, and checks that a background command never takes the
# terminal. After each asynchronous form the shell must still be the
# terminal's foreground group and answer the next command. A foreground program
# started after a background job must own the terminal and stop on Ctrl-Z or
# end on Ctrl-C. A prompt hook prints a numbered marker, and each line is typed
# only after the marker of the previous prompt. Each check prints one stable
# PASS line.

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
MOODS = ("kosh", "bash", "bash-posix", "sh")
CTRL_C = b"\x03"
CTRL_Z = b"\x1a"


class Session:
    def __init__(self, binary, directory, mood):
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
            os.execve(binary, [binary, "-M", mood, "--norc", "-i"],
                      environment)
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
        self.send(line.encode() + b"\r")
        return self.wait_for_prompt()

    def send(self, data):
        os.write(self.fd, data)

    def get_foreground_group(self):
        try:
            return os.tcgetpgrp(self.fd)
        except OSError:
            return -1

    def is_shell_in_foreground(self):
        return self.get_foreground_group() == self.pid

    def is_program_in_foreground(self):
        group = self.get_foreground_group()
        return group > 0 and group != self.pid

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
        sys.stderr.write("%s: foreground=%d shell=%d\n%s\n" % (
            name, session.get_foreground_group(), session.pid, tail))
        self.is_ok = False
        return False


def get_async_forms(mood, sleep):
    forms = [
        ("simple", "%s 0.2 &" % sleep),
        ("group", "{ %s 0.2; } &" % sleep),
        ("subshell", "( %s 0.2 ) &" % sleep),
        ("pipeline", "%s 0.2 | %s 0.2 &" % (sleep, sleep)),
    ]
    if mood != "sh":
        forms.append(("coproc", "coproc %s 0.2" % sleep))
    return forms


def check_async_forms(session, report, prefix, mood, sleep):
    for name, line in get_async_forms(mood, sleep):
        label = "%s-%s" % (prefix, name)
        is_answered = session.run(line)
        mark = len(session.raw)
        is_answered = session.run("jobs > /dev/null; wait; "
                                  "echo \"answer-$((6 * 7))\"") and is_answered
        report.check(label + "-keeps-the-shell-alive", session,
                     is_answered and session.has_text("answer-42", mark))
        report.check(label + "-keeps-the-terminal", session,
                     session.is_shell_in_foreground())


def check_foreground_program(session, report, prefix, sleep):
    session.run("%s 1 &" % sleep)
    session.send(("%s 5\r" % sleep).encode())
    is_lent = session.wait_until(session.is_program_in_foreground)
    report.check(prefix + "-foreground-program-owns-the-terminal", session,
                 is_lent)
    session.send(CTRL_Z)
    is_stopped = session.wait_for_prompt()
    report.check(prefix + "-ctrl-z-stops-the-program", session,
                 is_stopped and session.is_shell_in_foreground())

    session.send(b"fg\r")
    is_resumed = session.wait_until(session.is_program_in_foreground)
    report.check(prefix + "-fg-gives-the-terminal", session, is_resumed)
    session.send(CTRL_C)
    is_interrupted = session.wait_for_prompt()
    report.check(prefix + "-ctrl-c-ends-the-program", session,
                 is_interrupted and session.is_shell_in_foreground())

    session.send(("%s 5\r" % sleep).encode())
    session.wait_until(session.is_program_in_foreground)
    session.send(CTRL_Z)
    session.wait_for_prompt()
    is_answered = session.run("bg > /dev/null")
    report.check(prefix + "-bg-keeps-the-terminal", session,
                 is_answered and session.is_shell_in_foreground())

    session.send(b"fg > /dev/null\r")
    is_resumed = session.wait_until(session.is_program_in_foreground)
    session.send(CTRL_C)
    is_interrupted = session.wait_for_prompt()
    report.check(prefix + "-fg-after-bg-gives-and-takes-the-terminal",
                 session, is_resumed and is_interrupted
                 and session.is_shell_in_foreground())

    mark = len(session.raw)
    is_answered = session.run("wait; echo \"waited-$((6 * 7))\"")
    report.check(prefix + "-wait-keeps-the-terminal", session,
                 is_answered and session.has_text("waited-42", mark)
                 and session.is_shell_in_foreground())


def run_mood(binary, directory, mood, sleep, report):
    session = Session(binary, directory, mood)
    try:
        session.send(b"n=0; PROMPT_COMMAND='n=$((n + 1)); "
                     b"echo \"<ready-$n>\"'\r")
        if not report.check(mood + "-startup", session,
                            session.wait_for_prompt()):
            return

        for job_control in ("-m", "+m"):
            prefix = "%s%s" % (mood, job_control)
            session.run("set %s" % job_control)
            check_async_forms(session, report, prefix, mood, sleep)
            if job_control == "-m":
                check_foreground_program(session, report, prefix, sleep)
    finally:
        session.close()


def main():
    if sys.platform != "linux":
        print("background terminal PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)
    found_sleep = shutil.which("sleep", path="/usr/bin:/bin")
    sleep = os.path.join(os.path.realpath(os.path.dirname(found_sleep)),
                         os.path.basename(found_sleep))

    directory = tempfile.mkdtemp(prefix="kosh-background-terminal-pty-")
    report = Report()
    try:
        for mood in MOODS:
            run_mood(binary, directory, mood, sleep, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
