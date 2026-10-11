#!/usr/bin/env python3
"""A bounded PTY probe for the meow screensaver: frames, cleanup, any key."""

import fcntl
import os
import pty
import select
import signal
import struct
import sys
import termios
import time


CAT_FACE = b"( o.o )"
HOME_CURSOR = b"\x1b[H"


def run_pty(binary, key):
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(binary, [binary, "-Q", "-c",
                          "koshkit --color never meow"])

    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", 12, 40, 0, 0))
    output = bytearray()
    has_sent_key = False
    deadline = time.monotonic() + 4.0
    while time.monotonic() < deadline:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(fd, 65536)
        except OSError:
            break
        if not chunk:
            break
        output.extend(chunk)
        if not has_sent_key and bytes(output).count(HOME_CURSOR) >= 4:
            os.write(fd, key)
            has_sent_key = True

    exited_pid, status = os.waitpid(pid, os.WNOHANG)
    if exited_pid == 0:
        os.kill(pid, signal.SIGKILL)
        _, status = os.waitpid(pid, 0)
        exit_code = None
    else:
        exit_code = os.waitstatus_to_exitcode(status)

    return {
        "status": exit_code,
        "frames_before_key": bytes(output).count(HOME_CURSOR) >= 4,
        "cat": CAT_FACE in output,
        "header": b"q quit" in output,
        "alternate_enter": b"\x1b[?1049h" in output,
        "alternate_leave": b"\x1b[?1049l" in output,
        "cursor_hide": b"\x1b[?25l" in output,
        "cursor_show": b"\x1b[?25h" in output,
    }


def main():
    if sys.platform != "linux":
        print("meow PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2

    ok = True
    for name, key in (("meow-letter", b"x"), ("meow-space", b" "),
                      ("meow-escape", b"\x1b")):
        result = run_pty(binary, key)
        expected = {"status": 0, "frames_before_key": True, "cat": True,
                    "header": False, "alternate_enter": True,
                    "alternate_leave": True, "cursor_hide": True,
                    "cursor_show": True}
        failed = [field for field, value in expected.items()
                  if result.get(field) != value]
        if failed:
            print("%s FAIL %s result=%r" % (name, ",".join(failed), result))
            ok = False
        else:
            print("%s PASS" % name)

    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
