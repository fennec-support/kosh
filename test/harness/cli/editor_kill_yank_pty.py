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
# file. A trailing backslash continues the line, the shell joins it only
# outside quotes, and history keeps both physical lines. A lone Ctrl-X names
# its chords on the hint rows until the next key resolves it, and Ctrl-X before
# an arrow keeps the arrow. Ctrl-Z undoes at the prompt and still stops a
# running program, Ctrl-Shift-Z in its kitty and xterm encodings redoes, and
# Alt-T keeps trailing blanks in place. The prompt asks for the kitty and
# xterm extended keys and withdraws them before a command's output and around
# the external editor, and the option turns the request off. Kitty-encoded
# Enter, Ctrl-C, Ctrl-A, Ctrl-D, Ctrl-W, Ctrl-X, Ctrl-U, Ctrl-Z, Alt-B, and
# Escape act as their legacy bytes in the line, the menu, the chord, and vi
# mode, a bracketed paste still arrives, and Ctrl-D on an empty line ends the
# shell after the withdrawal. The terminal model and session come from the
# ghost and menu probe. Each check prints one stable PASS line for the golden
# output.

import os
import shutil
import sys
import tempfile
import time

from editor_ghost_menu_pty import (LEFT, WAIT_SECONDS, Report, Session,
                                   clear_line, has_hint, has_hint_header,
                                   is_line)


CTRL_A = b"\x01"
CTRL_K = b"\x0b"
CTRL_T = b"\x14"
CTRL_W = b"\x17"
CTRL_X = b"\x18"
CTRL_X_CTRL_E = b"\x18\x05"
CTRL_Y = b"\x19"
CTRL_U = b"\x15"
CTRL_Z = b"\x1a"
CTRL_SHIFT_Z_KITTY = b"\x1b[122;6u"
CTRL_SHIFT_Z_XTERM = b"\x1b[27;6;90~"
CTRL_X_HEADER = "pressed ctrl-x"
CTRL_X_HINT = "waiting for ctrl-e (edit in $VISUAL)"
ALT_DOT = b"\x1b."
ALT_T = b"\x1bt"
ALT_Y = b"\x1by"
EXTENDED_KEYS_ON = b"\x1b[>1u\x1b[>4;1m"
EXTENDED_KEYS_OFF = b"\x1b[<u\x1b[>4m"
KITTY_ALT_B = b"\x1b[98;3u"
KITTY_CTRL_A = b"\x1b[97;5u"
KITTY_CTRL_C = b"\x1b[99;5u"
KITTY_CTRL_D = b"\x1b[100;5u"
KITTY_CTRL_U = b"\x1b[117;5u"
KITTY_CTRL_W = b"\x1b[119;5u"
KITTY_CTRL_X = b"\x1b[120;5u"
KITTY_CTRL_Z = b"\x1b[122;5u"
KITTY_ENTER = b"\x1b[13u"
KITTY_ESCAPE = b"\x1b[27u"
VI_COMMAND_CURSOR = b"\x1b[2 q"


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


def get_raw_since(session, mark):
    return bytes(session.raw[mark:])


def is_withdrawn_around(session, mark, output):
    def do_check(screen):
        raw = get_raw_since(session, mark)
        withdrawn = raw.find(EXTENDED_KEYS_OFF)
        shown = raw.find(output)
        return (0 <= withdrawn < shown
                and raw.find(EXTENDED_KEYS_ON, shown) > shown)
    return do_check


def is_zombie(pid):
    try:
        with open("/proc/%d/stat" % pid) as handle:
            return handle.read().rsplit(")", 1)[1].split()[0] == "Z"
    except OSError:
        return True


def has_exited(session):
    def do_check(screen):
        deadline = time.monotonic() + WAIT_SECONDS
        while not is_zombie(session.pid):
            if time.monotonic() >= deadline:
                return False
            time.sleep(0.01)
        return True
    return do_check


def run_extended_key_checks(session, report):
    mark = len(session.raw)
    session.send(b"printf 'kitty-%s\\n' 42" + KITTY_ENTER)
    report.record("kitty-enter-runs-the-line", session,
                  has_output("kitty-42", 1))
    report.record("extended-keys-withdrawn-before-output", session,
                  is_withdrawn_around(session, mark, b"kitty-42\r\n"))

    session.send(b"echo doomed")
    session.wait_until(is_line("echo doomed"))
    session.send(KITTY_CTRL_C)
    report.record("kitty-ctrl-c-interrupts-the-line", session, is_line(""))

    session.send(b"echo one two")
    session.wait_until(is_line("echo one two"))
    session.send(KITTY_ALT_B)
    session.send(b"X")
    report.record("kitty-alt-b-moves-a-word", session,
                  is_line("echo one Xtwo"))
    clear_line(session)

    session.send(b"echo abc")
    session.wait_until(is_line("echo abc"))
    session.send(KITTY_CTRL_A + KITTY_CTRL_D)
    report.record("kitty-ctrl-a-and-ctrl-d-edit", session,
                  is_line("cho abc"))
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))

    session.send(b"echo ab")
    session.wait_until(is_line("echo ab"))
    session.send(KITTY_CTRL_W)
    session.wait_until(is_line("echo"))
    session.send(KITTY_CTRL_Z)
    report.record("kitty-ctrl-z-undoes", session, is_line("echo ab"))
    session.send(CTRL_SHIFT_Z_KITTY)
    report.record("kitty-ctrl-shift-z-redoes", session, is_line("echo"))
    session.send(KITTY_CTRL_Z)
    session.wait_until(is_line("echo ab"))
    session.send(KITTY_CTRL_W)
    session.wait_until(is_line("echo"))
    session.send(KITTY_CTRL_X)
    report.record("kitty-ctrl-x-shows-what-it-waits-for", session,
                  has_hint(CTRL_X_HINT))
    session.send(KITTY_CTRL_U)
    report.record("kitty-ctrl-x-ctrl-u-undoes", session,
                  lambda screen: is_line("echo ab")(screen)
                  and CTRL_X_HINT not in screen.get_hint())
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))

    session.send(b"s\t")
    session.wait_until(lambda screen: screen.get_menu() is not None)
    session.send(KITTY_ESCAPE)
    report.record("kitty-escape-closes-the-menu", session,
                  lambda screen: screen.get_menu() is None
                  and is_line("s")(screen))
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))

    session.send(b"\x1b[200~echo pasted\x1b[201~")
    report.record("paste-arrives-under-extended-keys", session,
                  is_line("echo pasted"))
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))

    session.send(b"set -o vi; echo vi-on\r")
    session.wait_until(has_output("vi-on", 1))
    session.send(b"echo abc")
    session.wait_until(is_line("echo abc"))
    mark = len(session.raw)
    session.send(KITTY_ESCAPE)
    session.wait_until(lambda screen: VI_COMMAND_CURSOR
                       in get_raw_since(session, mark))
    session.send(b"x")
    report.record("kitty-escape-enters-vi-command-mode", session,
                  is_line("echo ab"))
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))
    session.send(b"set -o emacs; echo emacs-on\r")
    session.wait_until(has_output("emacs-on", 1))

    session.send(b"koshconf set editor.extended_keys off; echo option-off\r")
    session.wait_until(has_output("option-off", 1))
    mark = bytes(session.raw).rfind(b"option-off\r\n")
    session.send(b"echo plain-keys\r")
    session.wait_until(has_output("plain-keys", 1))
    report.record("option-off-sends-no-request", session,
                  lambda screen: EXTENDED_KEYS_ON
                  not in get_raw_since(session, mark))
    session.send(b"koshconf set editor.extended_keys on; echo option-on\r")
    session.wait_until(has_output("option-on", 1))

    mark = len(session.raw)
    session.send(KITTY_CTRL_D)
    report.record("kitty-ctrl-d-ends-the-shell", session,
                  lambda screen: has_exited(session)(screen)
                  and EXTENDED_KEYS_OFF in get_raw_since(session, mark))


def run_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory)
    try:
        if not report.record("startup-prompt", session, is_line("")):
            return
        report.record("extended-keys-requested-at-the-prompt", session,
                      lambda screen: EXTENDED_KEYS_ON in bytes(session.raw))

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
        mark = len(session.raw)
        session.send(CTRL_X_CTRL_E)
        report.record("ctrl-x-ctrl-e-replaces-the-line", session,
                      lambda screen: is_line("echo draft EDITED")(screen)
                      and screen.count_lines("draft EDITED") == 0)
        report.record("extended-keys-withdrawn-for-the-editor", session,
                      lambda screen: 0 <= get_raw_since(session, mark).find(
                          EXTENDED_KEYS_OFF)
                      < get_raw_since(session, mark).find(EXTENDED_KEYS_ON))
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

        session.send(b"echo 'quoted\\\r")
        session.send(b"tail'\r")
        report.record("quoted-continuation-keeps-the-backslash", session,
                      has_output("quoted\\", 1))
        session.send(b"echo plain \\\r")
        session.send(b"joined\r")
        report.record("continuation-joins-in-the-shell", session,
                      has_output("plain joined", 1))
        report.record("history-file-keeps-continuation-lines", session,
                      lambda screen: b"echo 'quoted\\\\\\ntail'"
                      in read_bytes(history_path)
                      and b"echo plain \\\\\\njoined"
                      in read_bytes(history_path))

        session.send(b"echo ab")
        session.wait_until(is_line("echo ab"))
        session.send(CTRL_X)
        report.record("ctrl-x-shows-what-it-waits-for", session,
                      lambda screen: is_line("echo ab")(screen)
                      and has_hint_header(CTRL_X_HEADER)(screen)
                      and has_hint(CTRL_X_HINT)(screen))
        session.send(LEFT)
        session.send(b"Z")
        report.record("ctrl-x-hint-leaves-with-the-arrow", session,
                      lambda screen: is_line("echo aZb")(screen)
                      and CTRL_X_HINT not in screen.get_hint()
                      and screen.get_hint_header() != CTRL_X_HEADER)
        clear_line(session)

        session.send(b"echo ab")
        session.wait_until(is_line("echo ab"))
        session.send(CTRL_W)
        session.wait_until(is_line("echo"))
        session.send(CTRL_X)
        session.wait_until(has_hint(CTRL_X_HINT))
        session.send(CTRL_U)
        report.record("ctrl-x-ctrl-u-undoes-and-drops-the-hint", session,
                      lambda screen: is_line("echo ab")(screen)
                      and CTRL_X_HINT not in screen.get_hint())
        clear_line(session)

        session.send(b"echo ab")
        session.wait_until(is_line("echo ab"))
        session.send(CTRL_W)
        session.wait_until(is_line("echo"))
        session.send(CTRL_Z)
        report.record("ctrl-z-undoes-at-the-prompt", session,
                      is_line("echo ab"))
        session.send(CTRL_SHIFT_Z_KITTY)
        report.record("ctrl-shift-z-redoes", session, is_line("echo"))
        session.send(CTRL_Z + CTRL_SHIFT_Z_XTERM)
        report.record("ctrl-shift-z-redoes-from-modify-other-keys", session,
                      is_line("echo"))
        clear_line(session)
        session.send(b"echo still-here\r")
        report.record("ctrl-z-leaves-the-shell-running", session,
                      has_output("still-here", 1))

        session.send(b"slow-program\r")
        session.wait_until(lambda screen: screen.count_lines("started") == 1)
        session.send(CTRL_Z)
        report.record("ctrl-z-stops-a-running-program", session,
                      lambda screen: any("Stopped" in line
                                         for line in screen.get_lines())
                      and is_line("")(screen))
        session.send(b"kill -9 %1\r")
        session.wait_until(is_line(""))

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

        mark = len(session.raw)
        session.send(b"echo $((2 + 3))")
        session.wait_until(is_line("echo $((2 + 3))"))
        session.send(b"\r")
        report.record("arithmetic-brackets-highlight-cleanly", session,
                      lambda screen: has_output("5", 1)(screen)
                      and b"runtime error" not in bytes(session.raw[mark:]))

        run_extended_key_checks(session, report)
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
        write_script(os.path.join(command_directory, "slow-program"),
                     "echo started\n"
                     "exec /bin/sleep 30\n")
        write_script(os.path.join(command_directory, "control-visual"),
                     "printf ': a\\033]0;PWN\\007b\\n' > \"$1\"\n")
        run_checks(binary, directory, command_directory, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
