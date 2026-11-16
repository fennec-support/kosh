#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives an interactive shell through a real PTY and interrupts utilities and
# a subshell with Ctrl-C. The next prompt waits for child terminal cleanup and
# keeps cursor-key editing in raw mode. Each check prints one stable PASS line.

import fcntl
import os
import pty
import select
import shutil
import shlex
import struct
import sys
import tempfile
import termios
import time


BIN = os.environ["BIN"]
WAIT_SECONDS = 10.0
PROMPT_START_MARK = b"\x1b]133;A"


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
    interrupted = read_until(descriptor, b"", PROMPT_START_MARK)
    output += interrupted
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


def subshell_restores_terminal(home):
    child = os.path.join(home, "terminal-child.py")
    with open(child, "w") as stream:
        stream.write('''import signal, sys, termios, time
original = termios.tcgetattr(0)
def interrupted(number, frame):
    time.sleep(0.25)
    termios.tcsetattr(0, termios.TCSANOW, original)
    print("CHILD-" + "RESTORED", flush=True)
    sys.exit(130)
signal.signal(signal.SIGINT, interrupted)
print("CHILD-" + "READY", flush=True)
while True:
    time.sleep(1)
''')
    pid, descriptor = pty.fork()
    if pid == 0:
        environment = dict(os.environ, TERM="xterm-256color", HOME=home,
                           KOSH_HISTORY_FILE=os.path.join(home, "history"))
        os.chdir(home)
        os.execve(BIN, [BIN, "--no-init-files", "--no-diagnostics", "-i"],
                  environment)
    fcntl.ioctl(descriptor, termios.TIOCSWINSZ,
                struct.pack("HHHH", 24, 100, 0, 0))
    try:
        command = ('(' + shlex.quote(sys.executable) + ' -u ' +
                   shlex.quote(child) + '); echo FOLLOWED-$((6*7))\r')
        os.write(descriptor, command.encode())
        output = read_until(descriptor, b"", b"CHILD-READY")
        if b"CHILD-READY" not in output:
            return False
        os.write(descriptor, b"\x03")
        output = read_until(descriptor, b"", b"CHILD-RESTORED")
        output = read_until(descriptor, output, b"\x1b[?2004h")
        if (b"CHILD-RESTORED" not in output or
                b"\x1b[?2004h" not in output or
                output.index(b"CHILD-RESTORED") >
                output.index(b"\x1b[?2004h") or
                b"FOLLOWED-42" in output):
            return False
        mode = termios.tcgetattr(descriptor)
        if mode[3] & (termios.ECHO | termios.ICANON):
            return False
        if os.tcgetpgrp(descriptor) != pid:
            return False
        os.write(descriptor, b"echo CURSOR-$((40+1))\x1b[D\x1b[D\x7f2\r")
        output = read_until(descriptor, b"", b"CURSOR-42")
        return b"CURSOR-42" in output
    finally:
        try:
            os.kill(pid, 9)
            os.waitpid(pid, 0)
        except (ChildProcessError, ProcessLookupError):
            pass
        os.close(descriptor)


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
        check("subshell interrupt waits for terminal restoration",
              subshell_restores_terminal(home))
    finally:
        shutil.rmtree(home, ignore_errors=True)


main()
