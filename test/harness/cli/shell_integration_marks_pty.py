#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives an interactive shell through a real PTY and checks the OSC 133 and
# OSC 7 shell integration marks as raw bytes. It checks the prompt, command,
# and exit status order, the status after a failing command, the working
# directory after cd, and that the marks vanish under KOSH_SHELL_INTEGRATION=0
# and TERM=dumb. Each check prints one stable PASS line.

import fcntl
import os
import pty
import re
import select
import struct
import sys
import tempfile
import termios
import time


BIN = os.environ["BIN"]
WAIT_SECONDS = 10.0
MARK_PATTERN = re.compile(rb"\x1b\](?:133;[ABCD](?:;\d+)?|7;[^\x1b\x07]*)\x07")


def run_session(extra_environment, commands, home):
    pid, descriptor = pty.fork()
    if pid == 0:
        environment = dict(os.environ, TERM="xterm-256color", HOME=home)
        environment.pop("TERM_PROGRAM", None)
        environment.pop("KOSH_SHELL_INTEGRATION", None)
        environment.update(extra_environment)
        os.chdir(home)
        os.execve(BIN, [BIN, "-i"], environment)
    fcntl.ioctl(descriptor, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
    output = b""

    def drain(seconds, marker):
        nonlocal output
        deadline = time.time() + seconds
        while time.time() < deadline:
            ready, _, _ = select.select([descriptor], [], [], 0.05)
            if ready:
                try:
                    data = os.read(descriptor, 65536)
                except OSError:
                    return
                if not data:
                    return
                output += data
            if marker is not None and output.count(marker) >= 1:
                return

    drain(WAIT_SECONDS, b"Welcome")
    time.sleep(0.3)
    for command in commands:
        before = len(output)
        os.write(descriptor, command.encode() + b"\r")
        deadline = time.time() + WAIT_SECONDS
        while time.time() < deadline:
            drain(0.2, None)
            if b"\x1b]0;" in output[before:] and output[before:].count(b"\x07") >= 2:
                break
        time.sleep(0.2)
        drain(0.2, None)
    os.write(descriptor, b"exit\r")
    drain(1.0, b"Goodbye")
    try:
        os.waitpid(pid, 0)
    except ChildProcessError:
        pass
    return output


def get_marks(output):
    return [match.group(0) for match in MARK_PATTERN.finditer(output)]


def check(name, is_ok):
    print(("PASS " if is_ok else "FAIL ") + name)
    if not is_ok:
        sys.exit(1)


def main():
    home = tempfile.mkdtemp()
    target = os.path.realpath(tempfile.mkdtemp())
    output = run_session({}, ["false", "cd " + target], home)
    marks = get_marks(output)
    names = [mark[2:-1].decode() for mark in marks]
    kinds = [
        "7" if name.startswith("7;") else name
        for name in names
        if name != "133;B"
    ]
    check("mark order", kinds == ["7", "133;A", "133;C", "133;D;1", "7", "133;A", "133;C", "133;D;0", "7", "133;A", "133;C"])
    check("prompt end follows in the prompt", b"\x1b]133;B\x07" in output)
    check("directory is a file uri", names[0].startswith("7;file://"))
    check("cd reports directory", [name for name in names if name.startswith("7;")][2].endswith(target))

    quiet = run_session({"KOSH_SHELL_INTEGRATION": "0"}, ["false"], home)
    check("opt out writes nothing", len(get_marks(quiet)) == 0)

    dumb = run_session({"TERM": "dumb"}, ["false"], home)
    check("dumb terminal writes nothing", len(get_marks(dumb)) == 0)


main()
