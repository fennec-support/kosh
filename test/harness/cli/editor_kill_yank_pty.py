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
# stops itself is continued and its temporary file removed, one stopped by a
# terminal read or stopped again more times than the shell continues is ended
# with its stop status, and one that writes
# control bytes leaves them drawn in caret notation and out of the history
# file. A trailing backslash continues the line unless it is itself escaped,
# the shell joins it only
# outside quotes, and history keeps both physical lines. A lone Ctrl-X names
# its chords on the hint rows until the next key resolves it, and Ctrl-X before
# an arrow keeps the arrow. Ctrl-Z undoes at the prompt and still stops a
# running program, Ctrl-Shift-Z in its kitty and xterm encodings redoes, and
# Alt-T keeps trailing blanks in place. Leaving the vi : command line by
# Escape or Backspace keeps the caret where the colon was typed. The prompt asks for the kitty and
# xterm extended keys and withdraws them before a command's output and around
# the external editor, and the option turns the request off. Every editing
# Ctrl and Alt key in the table acts the same in its legacy bytes, which a
# terminal that ignores the request sends, its kitty form, and its
# modifyOtherKeys form, with Ctrl-^ as the legacy redo. PROMPT_COMMAND
# sees the terminal in its usual mode, and the request follows its output.
# Kitty-encoded
# Enter, Ctrl-C, Ctrl-A, Ctrl-D, Ctrl-W, Ctrl-X, Ctrl-U, Ctrl-Z, Alt-B, and
# Escape act as their legacy bytes in the line, the menu, the chord, and vi
# mode, a bracketed paste still arrives, and Ctrl-D on an empty line ends the
# shell after the withdrawal. A shell that dies of SIGABRT or SIGSEGV at the
# prompt still withdraws the requests and restores the terminal modes. The
# terminal model and session come from the
# ghost and menu probe. Each check prints one stable PASS line for the golden
# output.

import os
import shutil
import signal
import sys
import tempfile
import termios
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
BRACKETED_PASTE_OFF = b"\x1b[?2004l"


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


def is_requested_after(session, mark, output):
    def do_check(screen):
        raw = get_raw_since(session, mark)
        shown = raw.find(output)
        return (shown >= 0
                and raw.rfind(EXTENDED_KEYS_ON, 0, shown)
                < raw.rfind(EXTENDED_KEYS_OFF, 0, shown)
                and raw.find(EXTENDED_KEYS_ON, shown) > shown)
    return do_check


def has_usual_modes(path):
    def do_check(screen):
        modes = read_text(path).split()
        return ("icanon" in modes and "isig" in modes
                and "-icanon" not in modes and "-isig" not in modes)
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


def kitty_key(code, modifier):
    return b"\x1b[%d;%du" % (code, modifier)


def xterm_key(code, modifier):
    return b"\x1b[27;%d;%d~" % (modifier, code)


CTRL = 5
ALT = 3
CTRL_SHIFT = 6


def same_code(name, prelude, legacy, code, modifier):
    return (name, prelude, legacy, kitty_key(code, modifier),
            xterm_key(code, modifier))


KEY_TABLE = (
    same_code("ctrl-a", b"", b"\x01", 97, CTRL),
    same_code("ctrl-b", b"", b"\x02", 98, CTRL),
    same_code("ctrl-d", b"", b"\x04", 100, CTRL),
    same_code("ctrl-e", b"", b"\x05", 101, CTRL),
    same_code("ctrl-f", b"", b"\x06", 102, CTRL),
    same_code("ctrl-h", b"", b"\x08", 104, CTRL),
    same_code("ctrl-k", b"", CTRL_K, 107, CTRL),
    same_code("ctrl-t", b"", CTRL_T, 116, CTRL),
    same_code("ctrl-u", b"", CTRL_U, 117, CTRL),
    same_code("ctrl-w", b"", CTRL_W, 119, CTRL),
    same_code("ctrl-y", CTRL_W, CTRL_Y, 121, CTRL),
    same_code("ctrl-z", CTRL_W, CTRL_Z, 122, CTRL),
    ("ctrl-underscore", CTRL_W, b"\x1f", kitty_key(45, CTRL_SHIFT),
     xterm_key(95, CTRL_SHIFT)),
    ("ctrl-shift-z", CTRL_W + CTRL_Z, b"\x1e", kitty_key(122, CTRL_SHIFT),
     xterm_key(90, CTRL_SHIFT)),
    ("ctrl-backspace", b"", b"\x08", kitty_key(127, CTRL),
     xterm_key(8, CTRL)),
    same_code("alt-b", b"", b"\x1bb", 98, ALT),
    same_code("alt-f", b"", b"\x1bf", 102, ALT),
    same_code("alt-d", b"", b"\x1bd", 100, ALT),
    same_code("alt-t", b"", ALT_T, 116, ALT),
    same_code("alt-y", CTRL_W + CTRL_Y, ALT_Y, 121, ALT),
    same_code("alt-dot", b"", ALT_DOT, 46, ALT),
    ("alt-backspace", b"", b"\x1b\x7f", kitty_key(127, ALT),
     xterm_key(127, ALT)),
)


def get_typed(screen):
    state = screen.get_typed_and_ghost()
    return None if state is None else state[0]


def run_key_table_checks(session, report):
    def do_apply(prelude, key):
        session.send(b"echo one two three" + LEFT * 4)
        session.wait_until(is_line("echo one two three"))
        session.send(prelude + key + b"X")
        session.wait_until(lambda screen: "X" in (get_typed(screen) or ""))
        session.pump(0.1)
        typed = get_typed(session.screen)
        clear_line(session)
        return typed

    for name, prelude, legacy, kitty, xterm in KEY_TABLE:
        if legacy == ALT_Y:
            do_apply(prelude, legacy)
        expected = do_apply(prelude, legacy)
        typed = [do_apply(prelude, kitty), do_apply(prelude, xterm)]
        if expected is None or typed != [expected, expected]:
            sys.stderr.write("%s: legacy %r, kitty and xterm %r\n"
                             % (name, expected, typed))
        report.record("%s-acts-the-same-in-every-encoding" % name, session,
                      lambda screen: expected is not None
                      and typed == [expected, expected])


def run_extended_key_checks(session, report, directory):
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

    session.send(b"koshconf set editor.request_extended_key_reports off; echo option-off\r")
    session.wait_until(has_output("option-off", 1))
    mark = bytes(session.raw).rfind(b"option-off\r\n")
    session.send(b"echo plain-keys\r")
    session.wait_until(has_output("plain-keys", 1))
    report.record("option-off-sends-no-request", session,
                  lambda screen: EXTENDED_KEYS_ON
                  not in get_raw_since(session, mark))
    session.send(b"koshconf set editor.request_extended_key_reports on; echo option-on\r")
    session.wait_until(has_output("option-on", 1))

    mark = len(session.raw)
    session.send(b"PROMPT_COMMAND='tty-modes > \"$HOME/tty-modes\"; "
                 b"echo prompt-command-$((40 + 2))'\r")
    report.record("prompt-command-sees-the-usual-terminal", session,
                  has_usual_modes(os.path.join(directory, "tty-modes")))
    report.record("extended-keys-requested-after-the-prompt-command",
                  session,
                  is_requested_after(session, mark, b"prompt-command-42\r\n"))
    session.send(b"unset PROMPT_COMMAND\r")
    session.wait_until(is_line(""))

    mark = len(session.raw)
    session.send(KITTY_CTRL_D)
    report.record("kitty-ctrl-d-ends-the-shell", session,
                  lambda screen: has_exited(session)(screen)
                  and EXTENDED_KEYS_OFF in get_raw_since(session, mark))


def run_vi_ex_checks(session, report):
    session.send(b"set -o vi; echo vi-ex-on\r")
    session.wait_until(has_output("vi-ex-on", 1))
    for name, leave_key in (("escape", KITTY_ESCAPE), ("backspace", b"\x7f")):
        session.send(b"echo abcd")
        session.wait_until(is_line("echo abcd"))
        mark = len(session.raw)
        session.send(KITTY_ESCAPE)
        session.wait_until(lambda screen: VI_COMMAND_CURSOR
                           in get_raw_since(session, mark))
        session.send(b"0w:")
        session.wait_until(lambda screen: any(
            line.strip() == ":" for line in screen.get_lines()))
        session.send(leave_key)
        session.wait_until(lambda screen: not any(
            line.strip() == ":" for line in screen.get_lines()))
        session.send(b"x")
        report.record("vi-ex-%s-keeps-the-caret" % name, session,
                      is_line("echo bcd"))
        session.send(KITTY_CTRL_C)
        session.wait_until(is_line(""))
    session.send(b"set -o emacs; echo vi-ex-off\r")
    session.wait_until(has_output("vi-ex-off", 1))


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
        session.send(b"echo escaped-end\\\\\r")
        report.record("escaped-trailing-backslash-submits", session,
                      has_output("escaped-end\\", 1))
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

        for name, script_name, status in (
                ("terminal-read-stop", "reading-visual", 149),
                ("repeated-stop", "restopping-visual", 147)):
            session.send(("VISUAL=%s\r" % os.path.join(command_directory,
                                                       script_name)).encode())
            session.wait_until(is_line(""))
            session.send(b"echo kept")
            session.wait_until(is_line("echo kept"))
            session.send(CTRL_X_CTRL_E)
            report.record("%s-editor-is-ended" % name, session,
                          has_message("exited with status %d" % status,
                                      "echo kept"))
            clear_line(session)

        mark = len(session.raw)
        session.send(b"echo $((2 + 3))")
        session.wait_until(is_line("echo $((2 + 3))"))
        session.send(b"\r")
        report.record("arithmetic-brackets-highlight-cleanly", session,
                      lambda screen: has_output("5", 1)(screen)
                      and b"runtime error" not in bytes(session.raw[mark:]))

        run_vi_ex_checks(session, report)
        run_key_table_checks(session, report)
        run_extended_key_checks(session, report, directory)
    finally:
        session.close()


def is_restored_after_death(session, mark):
    def do_check(screen):
        raw = get_raw_since(session, mark)
        try:
            local_modes = termios.tcgetattr(session.fd)[3]
        except termios.error:
            return False
        return (has_exited(session)(screen)
                and EXTENDED_KEYS_OFF in raw and BRACKETED_PASTE_OFF in raw
                and (local_modes & termios.ICANON) != 0
                and (local_modes & termios.ECHO) != 0)
    return do_check


def run_fatal_signal_checks(binary, directory, command_directory, report):
    for name, signal_number in (("abort", signal.SIGABRT),
                                ("segfault", signal.SIGSEGV)):
        session = Session(binary, directory, command_directory)
        try:
            if not session.wait_until(is_line("")):
                report.record("%s-startup-prompt" % name, session,
                              is_line(""))
                continue
            mark = len(session.raw)
            os.kill(session.pid, signal_number)
            report.record("%s-at-the-prompt-restores-the-terminal" % name,
                          session, is_restored_after_death(session, mark))
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
        write_script(os.path.join(command_directory, "tty-modes"),
                     "exec '%s' -a\n" % shutil.which("stty"))
        write_script(os.path.join(command_directory, "reading-visual"),
                     "while :; do kill -TTIN $$; done\n")
        write_script(os.path.join(command_directory, "restopping-visual"),
                     "while :; do kill -STOP $$; done\n")
        run_checks(binary, directory, command_directory, report)
        run_fatal_signal_checks(binary, directory, command_directory, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
