#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives an interactive shell through a real PTY, interrupts a running koshkit
# utility with Ctrl-C, and checks that the shell keeps running and evaluates
# the next command. Each check prints one stable PASS line.

import fcntl
import os
import pty
import select
import shutil
import struct
import sys
import tempfile
import termios
import time


BIN = os.environ["BIN"]
WAIT_SECONDS = 10.0


def read_until(descriptor, output, marker):
    deadline = time.time() + WAIT_SECONDS
    while marker not in output and time.time() < deadline:
        ready, _, _ = select.select([descriptor], [], [], 0.05)
        if not ready:
            continue
        try:
            data = os.read(descriptor, 65536)
        except OSError:
            break
        if not data:
            break
        output += data
    return output


def survives_interrupt(command, home):
    pid, descriptor = pty.fork()
    if pid == 0:
        environment = dict(os.environ, TERM="xterm-256color", HOME=home)
        os.chdir(home)
        os.execve(BIN, [BIN, "--no-init-files", "-i"], environment)
    fcntl.ioctl(descriptor, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 100, 0, 0))
    output = b""
    os.write(descriptor, ("echo STARTED-$((1+1)); " + command).encode() + b"\r")
    output = read_until(descriptor, output, b"STARTED-2")
    os.write(descriptor, b"\x03")
    os.write(descriptor, b"echo ALIVE-$((40+2))\r")
    output = read_until(descriptor, output, b"ALIVE-42")
    is_alive = b"ALIVE-42" in output
    try:
        os.kill(pid, 9)
        os.waitpid(pid, 0)
    except (ChildProcessError, ProcessLookupError):
        pass
    os.close(descriptor)
    return is_alive


def check(name, is_ok):
    print(("PASS " if is_ok else "FAIL ") + name)
    if not is_ok:
        sys.exit(1)


def main():
    home = tempfile.mkdtemp()
    try:
        check("koshkit yes leaves the shell running",
              survives_interrupt("koshkit yes > /dev/null", home))
        check("koshkit sleep leaves the shell running",
              survives_interrupt("koshkit sleep 30", home))
        check("koshkit cat leaves the shell running",
              survives_interrupt("koshkit cat", home))
    finally:
        shutil.rmtree(home, ignore_errors=True)


main()
