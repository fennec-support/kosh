#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives the interactive editor through a real PTY and checks the right prompt
# and the transient prompt from the text on screen. RPS1 must sit at the right
# edge of the first input row less one column, leave when typed text comes
# within one column of it and come back when the text shrinks, stay off the
# rows after the first, leave a terminal too narrow for it, stay out of the
# submitted command, and stay in the scrollback without the transient prompt.
# With the transient-prompt option, Enter must redraw the submitted line after
# "$ ", "# " for root, or PS1_TRANSIENT with no right prompt, no hint row, and
# no rows of a multi-row PS1, and Ctrl-C must redraw the interrupted line the
# same way before its ^C. A prompt that leaves too few columns beside it
# continues the input two columns from the left edge instead of in a narrow
# strip under the prompt. A working directory named with control bytes
# reaches the prompt through \w in caret notation and never as raw bytes. A
# backslash that PS1 takes from a parameter stays literal, also when the
# prompt is drawn again from its cached expansion. A job stopped by Ctrl-Z
# reports on its own row below the echoed ^Z, and Ctrl-C on a job that fg
# resumed ends its row before the next prompt. A background job that a signal
# ended reports the signal description at the next prompt.
# koshconf set --persist names the file it wrote at an interactive prompt. The
# terminal model and session come from the
# ghost and menu probe. Each check prints one stable PASS line for the golden
# output.

import fcntl
import os
import shutil
import struct
import sys
import tempfile
import termios

from editor_ghost_menu_pty import ROWS, Report, Session


COLUMNS = 40
RIGHT_PROMPT = "<R>"
RIGHT_PROMPT_COLUMN = COLUMNS - 1 - len(RIGHT_PROMPT)
ALT_ENTER = b"\x1b\r"
BACKSPACE = b"\x7f"
CTRL_C = b"\x03"
CTRL_Z = b"\x1a"
SLEEP_TITLE = b"]0;sleep 30\x07"
BULLET = "•"
SHORT_PROMPT = "# " if os.geteuid() == 0 else "$ "
CONTROL_DIRECTORY = b"d\x07\x1b[31mRED\x1b]2;PWNED\x07z"
CONTROL_DIRECTORY_VISIBLE = "d^G^[[31mRED^[]2;PWNED^Gz"
CONTROL_DIRECTORY_SEQUENCES = (b"\x1b[31mRED", b"\x1b]2;PWNED", b"\x07z")


def with_right_prompt(text):
    return text.ljust(RIGHT_PROMPT_COLUMN) + RIGHT_PROMPT


def get_prompt_line(screen, offset=0):
    row = screen.get_prompt_row()
    lines = screen.get_lines()
    if row < 0 or row + offset >= len(lines):
        return None
    return lines[row + offset]


def is_prompt_line(text, offset=0):
    return lambda screen: get_prompt_line(screen, offset) == text


def has_rows(expected):
    def do_check(screen):
        lines = screen.get_lines()
        return any(lines[index:index + len(expected)] == expected
                   for index in range(len(lines)))
    return do_check


def is_submitted(expected, prompt_line):
    return lambda screen: (has_rows(expected)(screen)
                           and is_prompt_line(prompt_line)(screen))


def set_columns(session, columns):
    fcntl.ioctl(session.fd, termios.TIOCSWINSZ,
                struct.pack("HHHH", ROWS, columns, 0, 0))


def run_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory, COLUMNS)
    empty_prompt = with_right_prompt(BULLET)
    try:
        if not report.record("startup-prompt", session,
                             lambda screen: screen.get_prompt_row() >= 0):
            return

        session.send(b"PS1='\\. '; RPS1='" + RIGHT_PROMPT.encode() + b"'\r")
        if not report.record("right-prompt-at-the-edge", session,
                             is_prompt_line(empty_prompt)):
            return

        fitting = BULLET + " echo " + "x" * (RIGHT_PROMPT_COLUMN - 8)
        session.send(fitting[2:].encode())
        report.record("right-prompt-keeps-one-free-column", session,
                      is_prompt_line(with_right_prompt(fitting)))
        session.send(b"x")
        report.record("right-prompt-leaves-on-overlap", session,
                      is_prompt_line(fitting + "x"))
        session.send(BACKSPACE)
        report.record("right-prompt-returns-when-text-shrinks", session,
                      is_prompt_line(with_right_prompt(fitting)))
        session.send(CTRL_C)
        session.wait_until(is_prompt_line(empty_prompt))

        session.send(b"echo one" + ALT_ENTER + b"echo two")
        report.record("right-prompt-stays-on-the-first-row", session,
                      lambda screen: is_prompt_line(
                          with_right_prompt(BULLET + " echo one"))(screen)
                      and is_prompt_line("  echo two", 1)(screen))
        session.send(b"\r")
        report.record("right-prompt-is-not-submitted", session,
                      is_submitted([with_right_prompt(BULLET + " echo one"),
                                    "  echo two", "one", "two"],
                                   empty_prompt))

        set_columns(session, 6)
        report.record("right-prompt-leaves-a-narrow-terminal", session,
                      is_prompt_line(BULLET))
        set_columns(session, COLUMNS)
        report.record("right-prompt-returns-at-full-width", session,
                      is_prompt_line(empty_prompt))

        session.send(b"koshconf set editor.transient_prompt_after_submit on\r")
        session.wait_until(is_submitted(
            [with_right_prompt(BULLET + " koshconf set editor.transient_prompt_after_submit on")],
            empty_prompt))
        session.send(b"echo three\r")
        report.record("transient-prompt-redraws-the-line", session,
                      is_submitted([SHORT_PROMPT + "echo three", "three"],
                                   empty_prompt))
        session.send(b"echo dropped")
        session.wait_until(is_prompt_line(with_right_prompt(
            BULLET + " echo dropped")))
        session.send(CTRL_C)
        report.record("transient-prompt-redraws-an-interrupted-line", session,
                      is_submitted([SHORT_PROMPT + "echo dropped^C"],
                                   empty_prompt))

        session.send(b"PS1=$'top\\n\\\\. '; PS1_TRANSIENT='T> '\r")
        session.wait_until(lambda screen: is_prompt_line(empty_prompt)(screen)
                           and is_prompt_line("top", -1)(screen))
        session.send(b"echo four" + ALT_ENTER + b"echo five\r")
        report.record("transient-prompt-collapses-prompt-rows", session,
                      lambda screen: is_submitted(
                          ["T> echo four", "   echo five", "four", "five",
                           "top"], empty_prompt)(screen)
                      and screen.count_lines("top") == 1)

        long_prompt = "p" * 26 + " " + BULLET + " "
        session.send(b"PS1='" + b"p" * 26 + b" \\. '; RPS1=\r")
        session.wait_until(is_prompt_line(long_prompt.rstrip()))
        session.send(b"echo " + b"x" * 15)
        report.record("narrow-prompt-continues-near-the-left-edge", session,
                      lambda screen: is_prompt_line(
                          long_prompt + "echo " + "x" * 6)(screen)
                      and is_prompt_line("  " + "x" * 9, 1)(screen))
        session.send(CTRL_C)
        session.wait_until(lambda screen: screen.get_lines()[-1]
                           == long_prompt.rstrip())

        os.makedirs(os.path.join(directory.encode(), CONTROL_DIRECTORY))
        mark = len(session.raw)
        session.send(b"PS1='\\w \\. '; RPS1=; cd ./d*z\r")
        report.record("prompt-directory-draws-control-bytes", session,
                      is_prompt_line("~/" + CONTROL_DIRECTORY_VISIBLE + " "
                                     + BULLET))
        report.record("prompt-directory-writes-no-raw-control", session,
                      lambda screen: not any(
                          sequence in bytes(session.raw[mark:])
                          for sequence in CONTROL_DIRECTORY_SEQUENCES))

        value_prompt = "C:\\new\\$HOME " + BULLET
        session.send(b"v='C:\\new\\$HOME'; PS1='$v \\. '\r")
        report.record("prompt-value-keeps-backslashes", session,
                      is_prompt_line(value_prompt))
        session.send(b"true\r")
        report.record("prompt-value-keeps-backslashes-when-drawn-again",
                      session,
                      is_submitted(["T> true"], value_prompt))
    finally:
        session.close()


def has_no_partial_line_marker(screen):
    return not any("\\n" in line for line in screen.get_lines())


def run_job_notice_checks(binary, directory, report):
    sleep_path = shutil.which("sleep")
    if sleep_path is None:
        print("job notice checks: skipped (no sleep program)")
        return
    session = Session(binary, directory, os.path.dirname(sleep_path), COLUMNS)
    try:
        session.wait_until(lambda screen: screen.get_prompt_row() >= 0)
        session.send(b"PS1='\\. '\r")
        session.wait_until(is_prompt_line(BULLET))

        mark = len(session.raw)
        session.send(b"sleep 30\r")
        session.wait_until(lambda screen: SLEEP_TITLE in session.raw[mark:])
        session.pump(0.3)
        session.send(CTRL_Z)
        report.record("stopped-job-notice-starts-a-row", session,
                      lambda screen: has_rows([
                          "^Z",
                          "[1]+  Stopped                    sleep 30",
                          BULLET])(screen))

        mark = len(session.raw)
        session.send(b"fg\r")
        session.wait_until(lambda screen: SLEEP_TITLE in session.raw[mark:])
        session.pump(0.3)
        session.send(CTRL_C)
        report.record("fg-interrupt-ends-its-row", session,
                      lambda screen: has_rows(["sleep 30", "^C", BULLET])(
                          screen) and has_no_partial_line_marker(screen))

        mark = len(session.raw)
        session.send(b"sleep 30 &\r")
        session.wait_until(lambda screen: b"[1] " in session.raw[mark:]
                           and is_prompt_line(BULLET)(screen))
        mark = len(session.raw)
        session.send(b"kill %1\r")
        session.wait_until(lambda screen: b"kill %1" in session.raw[mark:]
                           and is_prompt_line(BULLET)(screen))
        is_noticed = has_rows(["[1]+  Terminated                 sleep 30"])
        for _ in range(10):
            session.pump(0.2)
            if is_noticed(session.screen):
                break
            mark = len(session.raw)
            session.send(b":\r")
            session.wait_until(lambda screen: b":" in session.raw[mark:]
                               and is_prompt_line(BULLET)(screen))
        report.record("signaled-job-notice-names-the-signal", session,
                      is_noticed)

        mark = len(session.raw)
        session.send(b"koshconf set mood kosh --persist\r")
        written_note = ("Wrote mood=kosh to " + directory
                        + "/.config/kosh/kosh.conf").encode()
        report.record("persist-names-the-written-file", session,
                      lambda screen: written_note in session.raw[mark:])
    finally:
        session.close()


def main():
    if sys.platform != "linux":
        print("editor prompt PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)

    directory = tempfile.mkdtemp(prefix="kosh-editor-prompt-pty-")
    report = Report()
    try:
        command_directory = os.path.join(directory, "bin")
        os.makedirs(command_directory)
        run_checks(binary, directory, command_directory, report)
        run_job_notice_checks(binary, directory, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
