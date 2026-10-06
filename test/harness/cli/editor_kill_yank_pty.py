#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives the interactive editor through a real PTY and checks the readline
# editing keys from the text on screen. The checks cover the kill ring filled by
# Ctrl-K and consecutive Ctrl-W kills, Ctrl-Y and Alt-Y, Ctrl-T and Alt-T, the
# Alt-. walk through the last words of history, and Ctrl-X Ctrl-E through a
# VISUAL script that rewrites the line without running it. A VISUAL script that
# stops itself is continued and its temporary file removed, and one that writes
# control bytes leaves them drawn in caret notation and out of the history
# file. Ctrl-X before an arrow keeps the arrow, and Alt-T keeps trailing blanks
# in place. The terminal model
# and session come from the ghost and menu probe. Each check prints one stable
# PASS line for the golden output.

import os
import shutil
import sys
import tempfile

from editor_ghost_menu_pty import LEFT, Report, Session, clear_line, is_line


CTRL_A = b"\x01"
CTRL_K = b"\x0b"
CTRL_T = b"\x14"
CTRL_W = b"\x17"
CTRL_X = b"\x18"
CTRL_X_CTRL_E = b"\x18\x05"
CTRL_Y = b"\x19"
ALT_DOT = b"\x1b."
ALT_T = b"\x1bt"
ALT_Y = b"\x1by"


def read_bytes(path):
    try:
        with open(path, "rb") as handle:
            return handle.read()
    except OSError:
        return b""


def read_text(path):
    return read_bytes(path).decode("utf-8", "replace")


def write_script(path, body):
    with open(path, "w") as handle:
        handle.write("#!/bin/sh\n" + body)
    os.chmod(path, 0o755)


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

        stopping_script = os.path.join(command_directory, "stopping-visual")
        session.send(("VISUAL=%s\r" % stopping_script).encode())
        session.wait_until(is_line(""))
        session.send(b"echo paused")
        session.wait_until(is_line("echo paused"))
        session.send(CTRL_X_CTRL_E)
        report.record("stopped-editor-is-continued", session,
                      is_line("echo paused RESUMED"))
        edited_path_file = os.path.join(directory, "edited-path")
        report.record("stopped-editor-temp-file-is-removed", session,
                      lambda screen: os.path.exists(edited_path_file)
                      and not os.path.exists(read_text(edited_path_file)))
        submit(session, report, "continued-edit-runs-on-enter",
               "paused RESUMED")

        control_script = os.path.join(command_directory, "control-visual")
        session.send(("VISUAL=%s\r" % control_script).encode())
        session.wait_until(is_line(""))
        session.send(b": draft")
        session.wait_until(is_line(": draft"))
        mark = len(session.raw)
        session.send(CTRL_X_CTRL_E)
        report.record("control-bytes-in-the-line-draw-visibly", session,
                      lambda screen: is_line(": a^[]0;PWN^Gb")(screen)
                      and b"PWN\x07" not in bytes(session.raw[mark:]))
        session.send(b"\r")
        session.wait_until(is_line(""))
        session.send(b"echo after-control\r")
        report.record("history-keeps-working-after-control-bytes", session,
                      has_output("after-control", 1))
        history_path = os.path.join(directory, "history")
        report.record("history-file-holds-no-control-bytes", session,
                      lambda screen: b"\x1b" not in read_bytes(history_path)
                      and b"echo after-control" in read_bytes(history_path))

        session.send(b"echo ab")
        session.wait_until(is_line("echo ab"))
        session.send(CTRL_X + LEFT + LEFT + b"Z")
        report.record("ctrl-x-before-an-arrow-keeps-the-arrow", session,
                      is_line("echo Zab"))
        clear_line(session)

        session.send(b"echo one two ")
        session.wait_until(is_line("echo one two"))
        session.send(ALT_T)
        report.record("alt-t-keeps-trailing-blanks", session,
                      is_line("echo two one"))
        session.send(b"x")
        report.record("alt-t-leaves-the-caret-before-the-blank", session,
                      is_line("echo two onex"))
        clear_line(session)

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
        write_script(os.path.join(command_directory, "fake-visual"),
                     "IFS= read -r line < \"$1\"\n"
                     "printf '%s EDITED\\n' \"$line\" > \"$1\"\n")
        write_script(os.path.join(command_directory, "stopping-visual"),
                     "printf '%s' \"$1\" > \"$HOME/edited-path\"\n"
                     "kill -STOP $$\n"
                     "IFS= read -r line < \"$1\"\n"
                     "printf '%s RESUMED\\n' \"$line\" > \"$1\"\n")
        write_script(os.path.join(command_directory, "control-visual"),
                     "printf ': a\\033]0;PWN\\007b\\n' > \"$1\"\n")
        run_checks(binary, directory, command_directory, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
