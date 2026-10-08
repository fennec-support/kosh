#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives an interactive bash mood shell through a real PTY with a PS0 that
# holds a ${ command; } and a ${| command; } substitution, as the Ghostty bash
# integration sets it. PS0 must run the function in the current shell before
# each command, keep its assignments, yield the REPLY of the second form, and
# report no error. Each check prints one stable PASS line.

import fcntl
import os
import pty
import select
import struct
import sys
import tempfile
import termios
import time


BIN = os.environ["BIN"]
WAIT_SECONDS = 10.0
PROMPT_MARK = b"\x1b]133;A"


def run_session(commands, home):
    pid, descriptor = pty.fork()
    if pid == 0:
        environment = dict(os.environ, TERM="xterm-256color", HOME=home)
        environment.pop("TERM_PROGRAM", None)
        environment.pop("KOSH_SHELL_INTEGRATION", None)
        os.chdir(home)
        os.execve(BIN, [BIN, "--mood", "bash", "--norc", "-i"], environment)
    fcntl.ioctl(descriptor, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
    output = b""

    def drain_until(prompt_count):
        nonlocal output
        deadline = time.time() + WAIT_SECONDS
        while time.time() < deadline:
            if output.count(PROMPT_MARK) >= prompt_count:
                return
            ready, _, _ = select.select([descriptor], [], [], 0.05)
            if not ready:
                continue
            try:
                data = os.read(descriptor, 65536)
            except OSError:
                return
            if not data:
                return
            output += data

    drain_until(1)
    for index, command in enumerate(commands):
        os.write(descriptor, command.encode() + b"\r")
        drain_until(index + 2)
    os.write(descriptor, b"exit\r")
    drain_until(len(commands) + 3)
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return output


def check(name, is_ok):
    print(("PASS " if is_ok else "FAIL ") + name)
    if not is_ok:
        sys.exit(1)


def main():
    home = tempfile.mkdtemp()
    output = run_session([
        'f() { n=$((n+1)); echo "hook$n"; }',
        "PS0='[${ f; }|${| REPLY=v$n; }]'",
        "echo one",
        'echo "n=$n reply=${REPLY-unset}"',
    ], home)
    text = output.decode(errors="replace")
    check("first command runs the hook", "[hook1|v1]" in text)
    check("second command runs the hook again", "[hook2|v2]" in text)
    check("assignments in the hook persist", "n=2 reply=unset" in text)
    check("no expansion error", "Unable to expand" not in text)


main()
