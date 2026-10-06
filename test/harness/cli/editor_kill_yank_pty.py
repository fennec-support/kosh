#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives the interactive editor through a real PTY and checks the readline
# editing keys from the text on screen. The checks cover the kill ring filled by
# Ctrl-K and consecutive Ctrl-W kills, Ctrl-Y and Alt-Y, Ctrl-T and Alt-T, the
# Alt-. walk through the last words of history, and Ctrl-X Ctrl-E through a
# VISUAL script that rewrites the line without running it. The terminal model
# and session come from the ghost and menu probe. Each check prints one stable
# PASS line for the golden output.

import os
import shutil
import sys
import tempfile

from editor_ghost_menu_pty import Report, Session, clear_line, is_line


CTRL_A = b"\x01"
CTRL_K = b"\x0b"
CTRL_T = b"\x14"
CTRL_W = b"\x17"
CTRL_X_CTRL_E = b"\x18\x05"
CTRL_Y = b"\x19"
ALT_DOT = b"\x1b."
ALT_T = b"\x1bt"
ALT_Y = b"\x1by"


def has_output(text, count):
    return lambda screen: (screen.count_lines(text) == count
                           and is_line("")(screen))


def has_message(text, typed):
    return lambda screen: (any(text in line for line in screen.get_lines())
                           and is_line(typed)(screen))


def submit(session, report, name, text, count=1):
    session.send(b"\r")
    report.record(name, session, has_output(text, count))


def run_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory)
    try:
        if not report.record("startup-prompt", session, is_line("")):
            return

        session.send(b"alpha beta gamma")
        session.wait_until(is_line("alpha beta gamma"))
        session.send(CTRL_A + CTRL_K)
        report.record("ctrl-k-kills-the-line", session, is_line(""))
        session.send(b"echo ")
        session.wait_until(lambda screen: screen.get_typed_and_ghost() is not None
                           and screen.get_typed_and_ghost()[0] == "echo")
        session.send(CTRL_Y)
        report.record("ctrl-y-yanks-the-kill", session,
                      is_line("echo alpha beta gamma"))
        submit(session, report, "yanked-line-runs", "alpha beta gamma")

        session.send(b"echo x one two")
        session.wait_until(is_line("echo x one two"))
        session.send(CTRL_W + CTRL_W + CTRL_W)
        report.record("ctrl-w-kills-words", session, is_line("echo x"))
        session.send(CTRL_Y)
        report.record("ctrl-y-yanks-the-joined-kills", session,
                      is_line("echo x one two"))
        session.send(ALT_Y)
        report.record("alt-y-cycles-to-the-older-kill", session,
                      is_line("echo x alpha beta gamma"))
        session.send(ALT_Y)
        report.record("alt-y-wraps-to-the-newest-kill", session,
                      is_line("echo x one two"))
        submit(session, report, "cycled-line-runs", "x one two")

        session.send(b"echo ab")
        session.wait_until(is_line("echo ab"))
        session.send(CTRL_T)
        report.record("ctrl-t-transposes-characters", session,
                      is_line("echo ba"))
        clear_line(session)

        session.send(b"echo first second")
        session.wait_until(is_line("echo first second"))
        session.send(ALT_T)
        report.record("alt-t-transposes-words", session,
                      is_line("echo second first"))
        submit(session, report, "transposed-line-runs", "second first")

        session.send(b"echo ")
        session.wait_until(lambda screen: screen.get_typed_and_ghost() is not None
                           and screen.get_typed_and_ghost()[0] == "echo")
        session.send(ALT_DOT)
        report.record("alt-dot-inserts-the-last-word", session,
                      is_line("echo first"))
        session.send(ALT_DOT)
        report.record("alt-dot-walks-back", session, is_line("echo two"))
        session.send(ALT_DOT)
        report.record("alt-dot-walks-further-back", session,
                      is_line("echo gamma"))
        submit(session, report, "last-word-line-runs", "gamma")

        script = os.path.join(command_directory, "fake-visual")
        session.send(("VISUAL=%s\r" % script).encode())
        session.wait_until(is_line(""))
        session.send(b"echo draft")
        session.wait_until(is_line("echo draft"))
        session.send(CTRL_X_CTRL_E)
        report.record("ctrl-x-ctrl-e-replaces-the-line", session,
                      lambda screen: is_line("echo draft EDITED")(screen)
                      and screen.count_lines("draft EDITED") == 0)
        submit(session, report, "edited-line-runs-on-enter", "draft EDITED")

        session.send(b"unset VISUAL; EDITOR=zz-missing-editor\r")
        session.wait_until(is_line(""))
        session.send(b"echo kept")
        session.wait_until(is_line("echo kept"))
        session.send(CTRL_X_CTRL_E)
        report.record("missing-editor-keeps-the-line", session,
                      has_message("The editor 'zz-missing-editor' was not found",
                                  "echo kept"))
        clear_line(session)
    finally:
        session.close()


def main():
    if sys.platform != "linux":
        print("editor kill and yank PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)

    directory = tempfile.mkdtemp(prefix="kosh-editor-keys-pty-")
    report = Report()
    try:
        command_directory = os.path.join(directory, "bin")
        os.makedirs(command_directory)
        script = os.path.join(command_directory, "fake-visual")
        with open(script, "w") as handle:
            handle.write("#!/bin/sh\n"
                         "IFS= read -r line < \"$1\"\n"
                         "printf '%s EDITED\\n' \"$line\" > \"$1\"\n")
        os.chmod(script, 0o755)
        run_checks(binary, directory, command_directory, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
