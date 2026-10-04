#!/usr/bin/env python3
"""Bounded PTY and redirected probes for EvilIO, EvilNet, and EvilPS."""

import fcntl
import os
import pty
import select
import signal
import struct
import subprocess
import sys
import tempfile
import termios
import time


FRAME_MARKER = b"  LIVE"
HEADER_BAR_STYLE = b"\x1b[0;48;5;236;38;5;252m"


def run_pty(binary, command, keys=()):
    pid, fd = pty.fork()
    if pid == 0:
        os.execv(binary, [binary, "-Q", "-c", command])

    def resize(columns, rows):
        fcntl.ioctl(fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, columns, 0, 0))

    resize(100, 30)
    output = bytearray()
    resized = False
    key_index = 0
    key_frame_count = 0
    key_marker = FRAME_MARKER
    key_marker_count = 0
    deadline = time.monotonic() + (8.0 if keys else 2.0)
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
        if not resized and len(output) > 1000:
            resize(45, 10)
            resized = True
            time.sleep(0.15)
        frame_count = bytes(output).count(FRAME_MARKER)
        has_key_taken_effect = (
            frame_count > key_frame_count + 1
            if key_marker is None
            else bytes(output).count(key_marker) > key_marker_count)
        if (resized and key_index < len(keys) and has_key_taken_effect):
            os.write(fd, keys[key_index][0])
            key_marker = keys[key_index][1]
            key_marker_count = (0 if key_marker is None
                                else bytes(output).count(key_marker))
            key_index += 1
            key_frame_count = frame_count
        elif (resized and keys and keys[-1][0] != b"q"
              and key_index == len(keys) and has_key_taken_effect):
            break

    if resized:
        os.kill(pid, signal.SIGINT)
    else:
        os.kill(pid, signal.SIGKILL)
    drain_deadline = time.monotonic() + 1.0
    while time.monotonic() < drain_deadline:
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
    _, status = os.waitpid(pid, 0)
    all_parts = bytes(output).split(FRAME_MARKER)
    # The final part contains terminal cleanup after the last frame, not a frame.
    frame_parts = all_parts[1:-1]
    blank_counts = [part.count(b"\r\n\r\n") for part in frame_parts]
    return {
        "status": os.waitstatus_to_exitcode(status),
        "resized": resized,
        "frames": bytes(output).count(FRAME_MARKER),
        "controls": FRAME_MARKER in output,
        "hints": b"q quit" in output,
        "styled_header": HEADER_BAR_STYLE in output,
        "ansi": b"\x1b[" in output,
        "alternate_enter": b"\x1b[?1049h" in output,
        "alternate_leave": b"\x1b[?1049l" in output,
        "cursor_hide": b"\x1b[?25l" in output,
        "cursor_show": b"\x1b[?25h" in output,
        "blank_separator": bool(frame_parts) and all(
            count == 1 for count in blank_counts
        ),
        "sort_cycle": all(marker in output for marker in (
            b"SORT tree", b"SORT name", b"SORT pid", b"SORT cpu",
            b"SORT memory")),
        "search_query": b"SEARCH /1" in output,
        "search_cleared": bool(frame_parts) and b"SEARCH" not in frame_parts[-1],
    }


def run_redirected(binary, command):
    with tempfile.TemporaryFile() as output:
        process = subprocess.Popen([binary, "-Q", "-c", command],
                                   stdout=output, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline and process.poll() is None:
            output.seek(0)
            if output.read().count(FRAME_MARKER) >= 2:
                break
            time.sleep(0.05)
        process.send_signal(signal.SIGINT)
        status = process.wait(timeout=3.0)
        output.seek(0)
        data = output.read()
    return {
        "status": status,
        "lines": data.count(b"\n"),
        "ansi": b"\x1b[" in data,
        "controls": FRAME_MARKER in data,
        "hints": b"q quit" in data,
        "plain_header": b"\x1b" not in data.split(b"\n", 1)[0],
    }


def check(name, result, requirements):
    failed = [key for key, expected in requirements.items()
              if result.get(key) != expected]
    if failed:
        print("%s FAIL %s result=%r" % (name, ",".join(failed), result))
        return False
    print("%s PASS" % name)
    return True


def main():
    if sys.platform != "linux":
        print("live PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2

    ok = True
    for name, command, keys in (
        ("evilio-pty", "koshkit --color never evilio --ps --live=0.05 "
         "--cumulative=0.1", ()),
        ("evilnet-pty", "koshkit --color never evilnet --traffic --live=0.05 "
         "--cumulative=0.1", ()),
        ("evilss-pty", "koshkit --color never evilss --live=0.05", ()),
        ("evilps-pty", "koshkit --color never evilps --cpu --live=0.05 "
         "--cumulative=0.1 -1",
         ((b"s", b"SORT name"), (b"s", b"SORT pid"), (b"s", b"SORT cpu"),
          (b"s", b"SORT memory"), (b"s", None), (b"/", b"SEARCH /"),
          (b"1", b"SEARCH /1"), (b"\r", None), (b"\x1b", None))),
    ):
        result = run_pty(binary, command, keys)
        requirements = {"status": 130, "resized": True,
                        "controls": True, "hints": True, "ansi": True,
                        "alternate_enter": True,
                        "alternate_leave": True,
                        "cursor_hide": True,
                        "cursor_show": True,
                        "blank_separator": True}
        if keys:
            requirements["sort_cycle"] = True
            requirements["search_query"] = True
            requirements["search_cleared"] = True
        ok &= check(name, result, requirements)
        if result["frames"] < 2:
            print("%s FAIL fewer than two live frames" % name)
            ok = False

    for name, command in (
        ("evilio-quit", "koshkit --color never evilio --ps --live=0.05 "
         "--cumulative=0.1"),
        ("evilnet-quit", "koshkit --color never evilnet --traffic "
         "--live=0.05 --cumulative=0.1"),
        ("evilss-quit", "koshkit --color never evilss --live=0.05"),
        ("evilps-quit", "koshkit --color never evilps --cpu --live=0.05 "
         "--cumulative=0.1 -1"),
    ):
        result = run_pty(binary, command, ((b"q", None),))
        ok &= check(name, result, {"status": 0, "resized": True,
                                   "alternate_enter": True,
                                   "alternate_leave": True,
                                   "cursor_hide": True,
                                   "cursor_show": True})

    result = run_pty(binary, "koshkit --color always evilnet --traffic "
                     "--live=0.05 --cumulative=0.1")
    ok &= check("evilnet-color-pty", result,
                {"status": 130, "styled_header": True, "hints": False})
    result = run_pty(binary, "koshkit --color never evilnet --traffic "
                     "--live=0.05 --cumulative=0.1")
    ok &= check("evilnet-plain-pty", result,
                {"status": 130, "styled_header": False, "hints": True})

    for name, command in (
        ("evilss-redirected", "koshkit --color never evilss --live=0.05"),
        ("evilio-redirected", "koshkit --color never evilio --ps --live=0.05 "
         "--cumulative=0.1"),
        ("evilnet-redirected", "koshkit --color never evilnet --traffic "
         "--live=0.05 --cumulative=0.1"),
        ("evilps-redirected", "koshkit --color never evilps --cpu --live=0.05 "
         "--cumulative=0.1 -1"),
    ):
        result = run_redirected(binary, command)
        ok &= check(name, result, {"status": 130, "ansi": False,
                                   "controls": True, "hints": False})
        if result["lines"] < 2:
            print("%s FAIL fewer than two redirected frames" % name)
            ok = False

    result = run_redirected(binary, "koshkit --color always evilnet --traffic "
                            "--live=0.05 --cumulative=0.1")
    ok &= check("evilnet-forced-color-redirected", result,
                {"status": 130, "plain_header": True, "hints": False})
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
