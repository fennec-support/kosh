#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives the interactive editor through a real PTY and checks the readline
# editing keys from the text on screen. The checks cover the kill ring filled by
# Ctrl-K and consecutive Ctrl-W kills, Ctrl-Y and Alt-Y, Ctrl-T and Alt-T,
# Ctrl-T and Backspace keeping a flag, a skin tone, and a joined emoji whole, the
# Alt-. walk through the last words of history, and Ctrl-X Ctrl-E through a
# VISUAL script that rewrites the line without running it. A VISUAL script that
# stops itself is continued and its temporary file removed, one stopped by a
# terminal read or stopped again more times than the shell continues is ended
# with its stop status, and one that writes
# control bytes leaves them drawn in caret notation and out of the history
# file. A trailing backslash continues the line unless it is itself escaped,
# the shell joins it only
# outside quotes, and history keeps both physical lines. A lone Ctrl-X lists
# its chords on the hint rows until the next key resolves it, and Ctrl-X before
# an arrow keeps the arrow. Ctrl-Z undoes at the prompt and leaves the shell
# running, Ctrl-Shift-Z in its kitty and xterm encodings redoes, and
# Alt-T keeps trailing blanks in place. Leaving the vi : command line by
# Escape or Backspace keeps the caret where the colon was typed, :s replaces
# every match with g, and :u undoes it. Alt-X opens the M-x minibuffer, whose
# hint lists the commands, Tab completes a command, replace-string replaces
# every occurrence, Ctrl-X z repeats it, and M-x undo undoes the repeat.
# Vi mode ignores Ctrl-X, :e opens the line in VISUAL there, and Ctrl-X
# Ctrl-C ends the shell in emacs mode.
# slurp starts a new line on Enter, saves on Ctrl-X s and keeps editing,
# saves and leaves on :wq, leaves the file on :q!, rewrites a file in a shared
# directory in place and removes its backup, refuses to write over a file
# that another file replaced while it was open and keeps editing, and refuses
# a binary file. Tab inserts a tab while only blanks come before the caret,
# and a tab draws as blank columns. A colon command opened from a vi visual
# selection acts on the selected lines only, and o, j, J, and } edit the lines
# of a file. Ctrl-X s does nothing at the shell prompt. Alt-U, Alt-C, and Alt-L
# change the case of the next word, also from a kitty report, and Alt-\ deletes
# the spaces around the cursor. A save keeps the file mode, and a save that
# cannot write its replacement beside the file leaves the original unchanged. A
# file taller than the terminal shows a window of rows that follows the caret,
# Up on its first line recalls no shell history, and saving keeps every line.
# The completion menu in slurp covers the rows under the caret and gives them
# back when it closes. M-x get-option in slurp on a text file shows the option,
# and M-x format-buffer leaves the text file as it is, also with tab completion
# off. Ctrl-R and Ctrl-S find text backward and forward, Alt-< and Ctrl-End
# go to the start and the end of the file, vi / and n find it, a closed ? find
# keeps the direction of n, and gqq wraps a long line at 80 columns or at the
# width that editor.wrap_width sets, and . repeats a visual gq. :set turns an
# option off with a no prefix, shows it with a question mark, and completes the
# option with Tab, and M-x set-option lists the matching options, completes the
# option with Tab, and sets it. :format formats the row, and M-x format leaves
# a source that does not parse and reports it. A pasted script longer than 16
# kilobytes runs whole. The prompt asks for the kitty and
# xterm extended keys and withdraws them before a command's output and around
# the external editor, and the option turns the request off. Every editing Ctrl
# and Alt key in the table acts the same in its legacy bytes, which a terminal
# that ignores the request sends, its kitty form, and its modifyOtherKeys form,
# with Ctrl-^ as the legacy redo. PROMPT_COMMAND sees the terminal in its usual
# mode, and the request follows its output. Kitty-encoded Enter, Ctrl-C, Ctrl-A,
# Ctrl-D, Ctrl-W, Ctrl-X, Ctrl-U, Ctrl-Z, Alt-B, and Escape act as their legacy
# bytes in the line, the menu, the chord, and vi mode, a bracketed paste still
# arrives, and Ctrl-D on an empty line ends the shell after the withdrawal. A
# shell that dies of SIGABRT or SIGSEGV at the prompt still withdraws the
# requests and restores the terminal modes. The terminal model and session come
# from the ghost and menu probe. Each check prints one stable PASS line for the
# golden output.

import os
import re
import shutil
import signal
import sys
import tempfile
import termios
import time

from editor_ghost_menu_pty import (BACKSPACE, LEFT, WAIT_SECONDS, Report,
                                   Session, clear_line, has_hint,
                                   get_state, has_hint_header, is_line,
                                   settle, UP, ESCAPE)


CTRL_A = b"\x01"
CTRL_C = b"\x03"
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
CTRL_X_HINT = "ctrl-e to edit in $VISUAL"
ALT_DOT = b"\x1b."
ALT_T = b"\x1bt"
ALT_X = b"\x1bx"
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
    session.send(b"echo foo foo")
    session.wait_until(is_line("echo foo foo"))
    mark = len(session.raw)
    session.send(KITTY_ESCAPE)
    session.wait_until(lambda screen: VI_COMMAND_CURSOR
                       in get_raw_since(session, mark))
    session.send(b":")
    session.wait_until(has_minibuffer(":"))
    session.send(b"s/foo/bar/g\r")
    report.record("vi-ex-substitute-replaces-every-match", session,
                  is_line("echo bar bar"))
    session.send(b":")
    session.wait_until(has_minibuffer(":"))
    session.send(b"u\r")
    report.record("vi-ex-undo-restores-the-line", session,
                  is_line("echo foo foo"))
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))
    mark = len(session.raw)
    session.send(KITTY_ESCAPE)
    session.wait_until(lambda screen: VI_COMMAND_CURSOR
                       in get_raw_since(session, mark))
    session.send(b":")
    session.wait_until(has_minibuffer(":"))
    session.send(b"set noeditor.auto_close_brackets_and_quotes\r")
    report.record("vi-ex-set-turns-an-option-off", session,
                  has_minibuffer("editor.auto_close_brackets_and_quotes=off"))
    session.send(b":")
    session.wait_until(has_minibuffer(":"))
    session.send(b"set editor.auto_close_brackets_and_quotes?\r")
    report.record("vi-ex-set-query-shows-the-value", session,
                  has_minibuffer("editor.auto_close_brackets_and_quotes=off"))
    session.send(b":")
    session.wait_until(has_minibuffer(":"))
    session.send(b"set editor.auto_cl\t")
    report.record("vi-ex-set-completes-the-option", session,
                  has_minibuffer(":set editor.auto_close_brackets_and_quotes"))
    session.send(KITTY_ESCAPE)
    session.wait_until(lambda screen: not has_minibuffer(
        ":set editor.auto_close_brackets_and_quotes")(screen))
    session.send(b"i")
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))
    session.send(b"echo   formatted")
    session.wait_until(is_line("echo   formatted"))
    mark = len(session.raw)
    session.send(KITTY_ESCAPE)
    session.wait_until(lambda screen: VI_COMMAND_CURSOR
                       in get_raw_since(session, mark))
    session.send(b":")
    session.wait_until(has_minibuffer(":"))
    session.send(b"format\r")
    report.record("vi-ex-format-formats-the-row", session,
                  is_line("echo formatted"))
    session.send(b"i")
    session.send(KITTY_CTRL_C)
    session.wait_until(is_line(""))
    session.send(b"set -o emacs; echo vi-ex-off\r")
    session.wait_until(has_output("vi-ex-off", 1))


def has_minibuffer(text):
    return lambda screen: any(line.strip() == text
                              for line in screen.get_lines())


def run_minibuffer_checks(session, report):
    session.send(b"echo aa aa")
    session.wait_until(is_line("echo aa aa"))
    session.send(ALT_X)
    report.record("alt-x-hint-lists-commands", session,
                  lambda screen: has_minibuffer("pressed M-x")(screen)
                  and has_hint("replace-string")(screen))
    session.send(b"replace-s\t")
    report.record("alt-x-tab-completes-the-command", session,
                  has_minibuffer("M-x replace-string"))
    session.send(b"\r")
    session.wait_until(has_minibuffer("Replace string:"))
    session.send(b"aa\r")
    session.wait_until(has_minibuffer("Replace string aa with:"))
    session.send(b"b\r")
    report.record("alt-x-replace-string-replaces-every-match", session,
                  is_line("echo b b"))
    session.send(b" aa")
    session.wait_until(is_line("echo b b aa"))
    session.send(CTRL_X + b"z")
    report.record("ctrl-x-z-repeats-the-replacement", session,
                  is_line("echo b b b"))
    session.send(ALT_X + b"undo\r")
    report.record("alt-x-undo-undoes-the-repeat", session,
                  is_line("echo b b aa"))
    clear_line(session)
    session.send(ALT_X + b"set-option\r")
    session.wait_until(has_minibuffer("Set option:"))
    session.send(b"editor.auto_close_br")
    report.record("alt-x-set-option-lists-matching-options", session,
                  has_hint("editor.auto_close_brackets_and_quotes"))
    session.send(b"\t")
    report.record("alt-x-set-option-completes-the-option", session,
                  has_minibuffer(
                      "Set option: editor.auto_close_brackets_and_quotes"))
    session.send(b"\ron\r")
    report.record("alt-x-set-option-sets-the-value", session,
                  has_minibuffer("editor.auto_close_brackets_and_quotes=on"))
    session.send(b"echo (")
    session.wait_until(is_line("echo ("))
    session.send(ALT_X + b"format\r")
    report.record("alt-x-format-reports-a-parse-error", session,
                  lambda screen: has_minibuffer("M-x format")(screen)
                  and is_line("echo (")(screen))
    clear_line(session)


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

        for name, typed, expected in (
                ("flag", "echo x\U0001F1FA\U0001F1F8",
                 "echo \U0001F1FA\U0001F1F8x"),
                ("skin-tone", "echo \U0001F44B\U0001F3FFz",
                 "echo z\U0001F44B\U0001F3FF"),
                ("joined-sequence",
                 "echo \U0001F468‍\U0001F469‍\U0001F467z",
                 "echo z\U0001F468‍\U0001F469‍\U0001F467")):
            session.send(typed.encode())
            session.wait_until(is_line(typed))
            session.send(CTRL_T)
            report.record("ctrl-t-keeps-a-%s-whole" % name, session,
                          is_line(expected))
            clear_line(session)

        session.send("echo \U0001F44B\U0001F3FF end".encode())
        session.wait_until(is_line("echo \U0001F44B\U0001F3FF end"))
        session.send(LEFT * 4 + BACKSPACE + b"Q")
        report.record("backspace-erases-a-skin-tone-emoji-whole", session,
                      is_line("echo Q end"))
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
        run_minibuffer_checks(session, report)

        script = "x=0\n" + "x=$((x + 1))\n" * 1500 + "echo total=$x"
        session.send(b"\x1b[200~" + script.encode() + b"\x1b[201~\r\r")
        report.record("pasted-long-script-runs-whole", session,
                      has_output("total=1500", 1))
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


def run_ctrl_x_mode_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory)
    try:
        session.wait_until(is_line(""))
        session.send(b"set -o vi; echo vi-chord-on\r")
        session.wait_until(has_output("vi-chord-on", 1))
        session.send(b"a")
        session.wait_until(is_line("a"))
        session.send(CTRL_X)
        settle(session, 0.3)
        session.send(b"b")
        report.record("vi-mode-ignores-ctrl-x", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0] == "ab"
                      and CTRL_X_HEADER not in "\n".join(
                          screen.get_lines()))
        session.send(KITTY_CTRL_C)
        session.wait_until(is_line(""))
        session.send(("VISUAL=%s\r" % os.path.join(command_directory,
                                                   "fake-visual")).encode())
        session.wait_until(is_line(""))
        session.send(b"echo draft")
        session.wait_until(is_line("echo draft"))
        mark = len(session.raw)
        session.send(KITTY_ESCAPE)
        session.wait_until(lambda screen: VI_COMMAND_CURSOR
                           in get_raw_since(session, mark))
        session.send(b":e\r")
        report.record("vi-ex-edit-replaces-the-line", session,
                      is_line("echo draft EDITED"))
        session.send(KITTY_CTRL_C)
        session.wait_until(is_line(""))
        session.send(b"set -o emacs; echo vi-chord-off\r")
        session.wait_until(has_output("vi-chord-off", 1))
        session.send(CTRL_X + b"s")
        settle(session, 0.3)
        session.send(b"echo still-here\r")
        report.record("ctrl-x-s-does-nothing-at-the-prompt", session,
                      has_output("still-here", 1))
        session.send(b"foo bar")
        session.wait_until(is_line("foo bar"))
        session.send(b"\x01\x1bu")
        report.record("alt-u-upcases-the-next-word", session,
                      is_line("FOO bar"))
        session.send(b"\x1bc")
        report.record("alt-c-capitalizes-the-next-word", session,
                      is_line("FOO Bar"))
        session.send(b"\x01" + kitty_key(ord("l"), 3))
        report.record("kitty-alt-l-downcases-the-next-word", session,
                      is_line("foo Bar"))
        session.send(b"\x05" + LEFT * 3 + b"   " + b"\x1b\\")
        report.record("alt-backslash-deletes-the-spaces-around-the-cursor",
                      session, is_line("fooBar"))
        session.send(KITTY_CTRL_C)
        session.wait_until(is_line(""))
        session.send(CTRL_X + b"\x03")
        report.record("ctrl-x-ctrl-c-ends-the-shell", session,
                      has_exited(session))
    finally:
        session.close()


def has_text(text):
    return lambda screen: any(text in line for line in screen.get_lines())


def has_row(text):
    return lambda screen: any(line.rstrip() == text
                              for line in screen.get_lines())


def find_row(session, text):
    """Waits for a row that holds text and gives its 1-based screen row."""
    found = []

    def do_check(screen):
        lines = [line.rstrip() for line in screen.get_lines()]
        if text in lines:
            found.append(lines.index(text) + 1)
            return True
        return False

    session.wait_until(do_check)
    return found[-1]


def mouse(button, column, row, is_release=False):
    return b"\x1b[<%d;%d;%d%s" % (button, column, row,
                                   b"m" if is_release else b"M")


def do_discard(session):
    session.send(CTRL_C)
    session.wait_until(has_text("ctrl-c again"))
    session.send(CTRL_C)
    session.wait_until(is_line(""))


def read_slurped(path):
    with open(path) as handle:
        return handle.read()


def run_slurp_checks(binary, directory, command_directory, report):
    script = os.path.join(directory, "slurp-script.sh")
    notes = os.path.join(directory, "slurp-notes.txt")
    binary_file = os.path.join(directory, "slurp-binary")
    with open(script, "w") as handle:
        handle.write("#!/bin/sh\necho one\necho two\n")
    with open(notes, "w") as handle:
        handle.write("first note\nsecond note\n")
    with open(binary_file, "wb") as handle:
        handle.write(b"a\0b")
    surrogate_file = os.path.join(directory, "slurp-surrogate")
    with open(surrogate_file, "wb") as handle:
        handle.write(b"keep\xed\xa0\x80me\n")
    locked_directory = os.path.join(directory, "slurp-locked")
    locked = os.path.join(locked_directory, "kept.txt")
    os.mkdir(locked_directory)
    with open(locked, "w") as handle:
        handle.write("original\n")
    os.chmod(locked_directory, 0o555)
    shared_directory = os.path.join(directory, "slurp-shared")
    shared = os.path.join(shared_directory, "doc.txt")
    os.mkdir(shared_directory)
    os.chmod(shared_directory, 0o777)
    with open(shared, "w") as handle:
        handle.write("doc\n")
    session = Session(binary, directory, command_directory)
    try:
        session.wait_until(is_line(""))
        session.send(b"slurp slurp-script.sh\r")
        report.record("slurp-shows-the-script-without-line-numbers", session,
                      has_row("echo one"))
        session.send(b" # edited\r")
        report.record("slurp-enter-starts-a-new-line", session,
                      has_text("line 4 of 4"))
        session.send(CTRL_X + b"s")
        report.record("slurp-ctrl-x-s-saves-and-keeps-editing", session,
                      lambda screen: has_text("wrote 38 bytes")(screen)
                      and has_text("line 4 of 4")(screen)
                      and read_slurped(script)
                      == "#!/bin/sh\necho one\necho two # edited\n\n")
        session.send(b"\t")
        report.record("slurp-tab-on-a-blank-line-inserts-a-tab", session,
                      lambda screen: has_text("line 4 of 4, column 2")(screen)
                      and not has_text("selecting completions")(screen))
        session.send(BACKSPACE)
        session.wait_until(has_text("line 4 of 4, column 1"))
        session.send(CTRL_X + CTRL_C)
        report.record("slurp-leaves-a-saved-buffer-at-once", session,
                      is_line(""))

        os.chmod(script, 0o751)
        session.send(b"slurp -v slurp-script.sh\r")
        session.wait_until(has_row("echo two # edited"))
        session.send(b"!")
        settle(session, 0.2)
        session.send(KITTY_ESCAPE)
        settle(session, 0.2)
        session.send(b":wq\r")
        report.record("slurp-vi-write-quit-saves", session,
                      lambda screen: is_line("")(screen)
                      and read_slurped(script).endswith("# edited\n!\n"))
        report.record("slurp-save-keeps-the-file-mode", session,
                      lambda screen: os.stat(script).st_mode & 0o7777 == 0o751)

        session.send(b"slurp slurp-locked/kept.txt\r")
        session.wait_until(lambda screen: any(line.rstrip() == "original"
                                              for line in screen.get_lines()))
        session.send(b" changed" + CTRL_X + b"s")
        report.record("slurp-failed-save-keeps-the-original", session,
                      lambda screen: has_text("unable to write the file")(screen)
                      and has_text("line 1 of 1")(screen)
                      and read_slurped(locked) == "original\n")
        do_discard(session)

        session.send(b"slurp -v slurp-notes.txt\r")
        report.record("slurp-shows-the-text-file", session,
                      has_row("second note"))
        session.send(b"?")
        settle(session, 0.2)
        session.send(KITTY_ESCAPE)
        settle(session, 0.2)
        session.send(b":q!\r")
        report.record("slurp-vi-quit-leaves-the-file", session,
                      lambda screen: is_line("")(screen)
                      and read_slurped(notes) == "first note\nsecond note\n")

        session.send(b"slurp slurp-notes.txt\r")
        session.wait_until(has_row("second note"))
        session.send(ALT_X + b"get-option\r")
        session.wait_until(has_text("Get option:"))
        session.send(b"mood\r")
        report.record("slurp-text-file-gets-an-option", session,
                      has_text("mood="))
        session.send(b";x")
        session.wait_until(has_row("second note;x"))
        session.send(ALT_X + b"format-buffer\r!")
        report.record("slurp-text-file-is-not-formatted", session,
                      has_row("second note;x!"))
        do_discard(session)

        session.send(b"koshconf set completion.on_tab off\r")
        session.wait_until(is_line(""))
        session.send(b"slurp slurp-notes.txt\r")
        session.wait_until(has_row("second note"))
        session.send(b";x")
        session.wait_until(has_row("second note;x"))
        session.send(ALT_X + b"format-buffer\r!")
        report.record("slurp-text-file-without-completion-is-not-formatted",
                      session, has_row("second note;x!"))
        do_discard(session)
        session.send(b"koshconf set completion.on_tab on\r")
        session.wait_until(is_line(""))

        found = os.path.join(directory, "slurp-find.txt")
        with open(found, "w") as handle:
            handle.write("alpha beta\ngamma beta\n" + " ".join(
                "word%02d" % index for index in range(16)) + "\n")
        session.send(b"slurp slurp-find.txt\r")
        session.wait_until(has_text("line 3 of 3"))
        session.send(b"\x12alpha\r")
        report.record("slurp-ctrl-r-finds-backward", session,
                      has_text("line 1 of 3, column 1,"))
        session.send(b"\x13beta\x13\r")
        report.record("slurp-ctrl-s-steps-to-the-next-match", session,
                      has_text("line 2 of 3, column 7,"))
        session.send(b"\x1b<")
        report.record("slurp-alt-less-goes-to-the-start", session,
                      has_text("line 1 of 3, column 1,"))
        session.send(b"\x1b[1;5F")
        report.record("slurp-ctrl-end-goes-to-the-end", session,
                      has_text("line 3 of 3, column 112,"))
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        session.send(b"slurp -v slurp-find.txt\r")
        session.wait_until(has_text("line 3 of 3"))
        session.send(KITTY_ESCAPE)
        settle(session, 0.2)
        session.send(b"gg/beta\r")
        report.record("slurp-vi-slash-finds-forward", session,
                      has_text("line 1 of 3, column 7,"))
        session.send(b"n")
        report.record("slurp-vi-n-finds-again", session,
                      has_text("line 2 of 3, column 7,"))
        session.send(b"gg/a\rn?\x7fn")
        report.record("slurp-vi-closed-find-keeps-the-direction", session,
                      has_text("line 2 of 3, column 2,"))
        session.send(b"Ggqq")
        report.record("slurp-vi-gq-wraps-the-line", session,
                      lambda screen: has_row("word00 word01 word02 word03 "
                                             "word04 word05 word06 word07 "
                                             "word08 word09 word10")(screen)
                      and has_row("word11 word12 word13 word14 word15")(screen))
        session.send(b":q!\r")
        session.wait_until(is_line(""))

        session.send(b"koshconf set editor.wrap_width 20\r")
        session.wait_until(is_line(""))
        session.send(b"slurp -v slurp-find.txt\r")
        session.wait_until(has_text("line 3 of 3"))
        session.send(KITTY_ESCAPE)
        settle(session, 0.2)
        session.send(b"Ggqq")
        report.record("slurp-vi-gq-wraps-at-the-set-width", session,
                      lambda screen: has_row("word00 word01 word02")(screen)
                      and has_row("word15")(screen))
        session.send(b":q!\r")
        session.wait_until(is_line(""))

        wrapped = os.path.join(directory, "slurp-wrap.txt")
        with open(wrapped, "w") as handle:
            handle.write("aaaa bbbb cccc dddd eeee ffff\n\n"
                         "aaaa bbbb cccc dddd eeee ffff\n")
        session.send(b"slurp -v slurp-wrap.txt\r")
        session.wait_until(has_text("line 3 of 3"))
        session.send(KITTY_ESCAPE)
        settle(session, 0.2)
        session.send(b"ggVgqG.")
        report.record("slurp-vi-dot-repeats-a-visual-gq", session,
                      lambda screen: sum(line.rstrip() == "eeee ffff"
                                         for line in screen.get_lines()) == 2)
        session.send(b":q!\r")
        session.wait_until(is_line(""))
        session.send(b"koshconf set editor.wrap_width 80\r")
        session.wait_until(is_line(""))

        session.send(b"slurp slurp-notes.txt\r")
        session.wait_until(lambda screen: any(line.rstrip() == "second note"
                                              for line in screen.get_lines()))
        session.send(b" saved")
        session.wait_until(lambda screen: any(
            line.rstrip() == "second note saved"
            for line in screen.get_lines()))
        session.send(CTRL_X + b"s")
        report.record("slurp-ctrl-x-s-saves", session,
                      lambda screen: has_text("wrote")(screen)
                      and read_slurped(notes)
                      == "first note\nsecond note saved\n")
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        session.send(b"slurp slurp-notes.txt\r")
        session.wait_until(lambda screen: any(
            line.rstrip() == "second note saved"
            for line in screen.get_lines()))
        session.send(b" lost")
        session.wait_until(lambda screen: any(
            line.rstrip() == "second note saved lost"
            for line in screen.get_lines()))
        session.send(CTRL_C)
        report.record("slurp-ctrl-c-asks-before-dropping-edits", session,
                      has_text("ctrl-c again to discard them"))
        session.send(CTRL_C)
        report.record("slurp-second-ctrl-c-drops-the-edits", session,
                      lambda screen: is_line("")(screen)
                      and read_slurped(notes)
                      == "first note\nsecond note saved\n")

        session.send(b"slurp -v slurp-notes.txt\r")
        session.wait_until(lambda screen: any(
            line.rstrip() == "second note saved"
            for line in screen.get_lines()))
        session.send(b"?")
        settle(session, 0.2)
        session.send(KITTY_ESCAPE)
        settle(session, 0.2)
        session.send(b":q\r")
        report.record("slurp-vi-quit-refuses-to-drop-edits", session,
                      has_text(":q! to discard them"))
        session.send(b":q!\r")
        session.wait_until(is_line(""))

        session.send(b"slurp slurp-notes.txt\r")
        session.wait_until(lambda screen: any(
            line.rstrip() == "second note saved"
            for line in screen.get_lines()))
        session.send(CTRL_X + CTRL_C)
        report.record("slurp-ctrl-x-ctrl-c-leaves-an-unedited-file", session,
                      is_line(""))

        backups = {name for name in os.listdir(directory)
                   if name.startswith(".kosh-slurp-backup")}
        session.send(b"slurp slurp-shared/doc.txt\r")
        session.wait_until(lambda screen: any(line.rstrip() == "doc"
                                              for line in screen.get_lines()))
        session.send(b" kept" + CTRL_X + b"s")
        session.wait_until(has_text("wrote"))
        session.send(CTRL_X + CTRL_C)
        report.record("slurp-in-place-save-removes-its-backup", session,
                      lambda screen: is_line("")(screen)
                      and read_slurped(shared) == "doc kept\n"
                      and {name for name in os.listdir(directory)
                           if name.startswith(".kosh-slurp-backup")}
                      == backups)

        session.send(b"slurp slurp-shared/doc.txt\r")
        session.wait_until(has_row("doc kept"))
        swapped = os.path.join(shared_directory, "swapped.txt")
        with open(swapped, "w") as handle:
            handle.write("other\n")
        os.replace(swapped, shared)
        session.send(b" edit" + CTRL_X + b"s")
        report.record("slurp-refuses-a-file-swapped-while-open", session,
                      lambda screen: has_text("another file took its place")(
                          screen)
                      and read_slurped(shared) == "other\n")
        do_discard(session)

        private = os.path.join(directory, "slurp-private.txt")
        with open(private, "w") as handle:
            handle.write("mine\n")
        session.send(b"slurp slurp-private.txt\r")
        session.wait_until(lambda screen: any(line.rstrip() == "mine"
                                              for line in screen.get_lines()))
        swapped = os.path.join(directory, "slurp-swapped.txt")
        with open(swapped, "w") as handle:
            handle.write("theirs\n")
        os.replace(swapped, private)
        session.send(b" edit" + CTRL_X + b"s")
        report.record("slurp-copy-save-refuses-a-swapped-file", session,
                      lambda screen: has_text("another file took its place")(
                          screen)
                      and read_slurped(private) == "theirs\n")
        do_discard(session)

        gone = os.path.join(directory, "slurp-gone.txt")
        with open(gone, "w") as handle:
            handle.write("kept\n")
        session.send(b"slurp slurp-gone.txt\r")
        session.wait_until(has_text("line 1 of 1"))
        os.unlink(gone)
        session.send(b" again" + CTRL_X + b"s")
        report.record("slurp-keeps-editing-after-a-refused-save", session,
                      lambda screen: has_text("unable to write the file")(screen)
                      and has_text("line 1 of 1, column 11")(screen)
                      and not os.path.exists(gone))
        do_discard(session)

        mixed = os.path.join(directory, "slurp-mixed.txt")
        with open(mixed, "wb") as handle:
            handle.write(b"one\r\ntwo\nthree\n")
        session.send(b"slurp slurp-mixed.txt\r")
        session.wait_until(lambda screen: any(line.rstrip() == "three"
                                              for line in screen.get_lines()))
        session.send(CTRL_X + b"s")
        report.record("slurp-keeps-mixed-line-endings", session,
                      lambda screen: has_text("wrote")(screen)
                      and read_bytes(mixed) == b"one\r\ntwo\nthree\n")
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        crlf = os.path.join(directory, "slurp-crlf.txt")
        with open(crlf, "wb") as handle:
            handle.write(b"a\r\nb\r\n")
        session.send(b"slurp slurp-crlf.txt\r")
        session.wait_until(has_text("CRLF endings"))
        session.send(b"x" + CTRL_X + b"s")
        report.record("slurp-keeps-crlf-endings", session,
                      lambda screen: has_text("wrote")(screen)
                      and read_bytes(crlf) == b"a\r\nbx\r\n")
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        unterminated = os.path.join(directory, "slurp-unterminated.txt")
        with open(unterminated, "wb") as handle:
            handle.write(b"abc")
        session.send(b"slurp slurp-unterminated.txt\r")
        session.wait_until(has_text("line 1 of 1"))
        session.send(b"d" + CTRL_X + b"s")
        report.record("slurp-keeps-a-missing-final-newline", session,
                      lambda screen: has_text("wrote")(screen)
                      and read_bytes(unterminated) == b"abcd")
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        oversized = os.path.join(directory, "slurp-oversized.txt")
        with open(oversized, "wb") as handle:
            handle.write(b"a" * 65535)
        session.send(b"slurp slurp-oversized.txt\r")
        report.record("slurp-refuses-a-file-over-the-limit", session,
                      lambda screen: any("is longer than" in line
                                         for line in screen.get_lines())
                      and is_line("")(screen))

        blank = os.path.join(directory, "slurp-blank.txt")
        with open(blank, "w") as handle:
            handle.write("\n")
        session.send(b"slurp slurp-blank.txt\r")
        session.wait_until(has_text("line 1 of 1"))
        session.send(CTRL_X + b"s")
        report.record("slurp-keeps-a-lone-newline", session,
                      lambda screen: has_text("wrote")(screen)
                      and read_bytes(blank) == b"\n")
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        tabbed = os.path.join(directory, "slurp-tabbed.txt")
        with open(tabbed, "w") as handle:
            handle.write("x\ny\n")
        session.send(b"slurp slurp-tabbed.txt\r")
        session.wait_until(has_text("line 2 of 2"))
        session.send(UP + b"\t")
        session.wait_until(has_text("line 1 of 2, column 3"))
        report.record("slurp-draws-a-tab-as-blank-columns", session,
                      lambda screen: has_row("x")(screen)
                      and not has_text("x>")(screen))
        session.send(CTRL_X + b"s")
        report.record("slurp-tab-inserts-a-tab-in-a-text-file", session,
                      lambda screen: has_text("wrote")(screen)
                      and read_bytes(tabbed) == b"x\t\ny\n")
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        keyed = os.path.join(directory, "slurp-keyed.txt")
        with open(keyed, "w") as handle:
            handle.write("one\ntwo\n  three\n\nfour\n")
        session.send(b"slurp -v slurp-keyed.txt\r")
        session.wait_until(lambda screen: any(line.rstrip() == "four"
                                              for line in screen.get_lines()))
        mark = len(session.raw)
        session.send(KITTY_ESCAPE)
        session.wait_until(lambda screen: VI_COMMAND_CURSOR
                           in get_raw_since(session, mark))
        session.send(b"ggonew")
        session.wait_until(lambda screen: any(line.rstrip() == "new"
                                              for line in screen.get_lines()))
        mark = len(session.raw)
        session.send(KITTY_ESCAPE)
        session.wait_until(lambda screen: VI_COMMAND_CURSOR
                           in get_raw_since(session, mark))
        session.send(b"jJ}iX")
        session.wait_until(lambda screen: any(line.rstrip() == "X"
                                              for line in screen.get_lines()))
        mark = len(session.raw)
        session.send(KITTY_ESCAPE)
        session.wait_until(lambda screen: VI_COMMAND_CURSOR
                           in get_raw_since(session, mark))
        session.send(b":wq\r")
        report.record("vi-o-j-J-and-brace-keys-edit-the-file", session,
                      lambda screen: is_line("")(screen)
                      and read_slurped(keyed)
                      == "one\nnew\ntwo three\nX\nfour\n")

        ranged = os.path.join(directory, "slurp-ranged.txt")
        with open(ranged, "w") as handle:
            handle.write("x a\nx b\nx c\nx d\n")
        session.send(b"slurp -v slurp-ranged.txt\r")
        session.wait_until(lambda screen: any(line.rstrip() == "x d"
                                              for line in screen.get_lines()))
        mark = len(session.raw)
        session.send(KITTY_ESCAPE)
        session.wait_until(lambda screen: VI_COMMAND_CURSOR
                           in get_raw_since(session, mark))
        session.send(b"ggjVj:")
        report.record("vi-visual-colon-opens-a-range-prompt", session,
                      lambda screen: any(line.strip() == ":'<,'>"
                                         for line in screen.get_lines()))
        session.send(b"s/x/y/\r")
        session.wait_until(lambda screen: any(line.rstrip() == "y c"
                                              for line in screen.get_lines()))
        session.send(b":wq\r")
        report.record("vi-visual-range-substitutes-only-its-lines", session,
                      lambda screen: is_line("")(screen)
                      and read_slurped(ranged) == "x a\ny b\ny c\nx d\n")

        stepped = os.path.join(directory, "slurp-stepped.txt")
        with open(stepped, "w") as handle:
            handle.write("abcdef\nxy\nlonger line\n")
        session.send(b"slurp -v slurp-stepped.txt\r")
        session.wait_until(has_text("line 3 of 3"))
        mark = len(session.raw)
        session.send(KITTY_ESCAPE)
        session.wait_until(lambda screen: VI_COMMAND_CURSOR
                           in get_raw_since(session, mark))
        session.send(b"gg$\r\r")
        report.record("vi-enter-steps-down-like-j", session,
                      has_text("line 3 of 3, column 6, as vi normal"))
        session.send(b":q\r")
        session.wait_until(is_line(""))

        selected = os.path.join(directory, "slurp-select.txt")
        with open(selected, "w") as handle:
            handle.write("alpha beta gamma\nsecond line\n")
        session.send(b"slurp slurp-select.txt\r")
        session.wait_until(has_row("second line"))
        session.send(b"\x1b[1;5H" + b"\x1b[1;2C" * 6)
        report.record("slurp-emacs-shift-right-selects", session,
                      has_text("ctrl-w to cut"))
        session.send(BACKSPACE)
        report.record("slurp-emacs-backspace-erases-the-selection", session,
                      lambda screen: has_row("beta gamma")(screen)
                      and not has_text("ctrl-w to cut")(screen))
        row = find_row(session, "second line")
        session.send(mouse(0, 1, row) + mouse(32, 4, row) + mouse(32, 7, row)
                     + mouse(0, 7, row, True))
        report.record("slurp-emacs-drag-selects", session,
                      has_text("ctrl-w to cut"))
        session.send(b"\x17\x05\x19")
        report.record("slurp-emacs-ctrl-w-kills-the-selection", session,
                      has_row(" linesecond"))
        do_discard(session)

        session.send(b"slurp -v slurp-select.txt\r")
        row = find_row(session, "alpha beta gamma")
        session.send(mouse(0, 1, row) + mouse(32, 5, row)
                     + mouse(0, 5, row, True))
        report.record("slurp-vi-drag-enters-visual-mode", session,
                      has_text("as vi visual"))
        session.send(b"d")
        report.record("slurp-vi-drag-selection-deletes", session,
                      lambda screen: has_row(" beta gamma")(screen)
                      and has_text("as vi normal")(screen))
        session.send(b":q!\r")
        session.wait_until(is_line(""))

        session.send(b"slurp slurp-binary\r")
        report.record("slurp-refuses-a-binary-file", session,
                      lambda screen: any("is not a text file" in line
                                         for line in screen.get_lines())
                      and is_line("")(screen))

        # The editor holds no surrogate, so a file with one is refused
        # instead of opening empty.
        session.send(b"slurp slurp-surrogate\r")
        report.record("slurp-refuses-a-surrogate", session,
                      lambda screen: sum("is not a text file" in line
                                         for line in screen.get_lines()) == 2
                      and is_line("")(screen))

        # A special file can stream forever, so it is refused before reading.
        session.send(b"slurp /dev/zero\r")
        report.record("slurp-refuses-a-special-file", session,
                      lambda screen: any("is not a regular file" in line
                                         for line in screen.get_lines())
                      and is_line("")(screen))
    finally:
        session.close()
        os.chmod(locked_directory, 0o755)


def has_numbered_rows(first, last):
    def do_check(screen):
        numbers = []
        for line in screen.get_lines():
            match = re.match(r"echo line(\d+)", line)
            if line.rstrip() == "#!/bin/sh":
                numbers.append(1)
            elif match:
                numbers.append(int(match.group(1)))
        return numbers == list(range(first, last + 1))
    return do_check


def run_tall_slurp_checks(binary, directory, command_directory, report):
    script = os.path.join(directory, "slurp-tall.sh")
    with open(script, "w") as handle:
        handle.write("#!/bin/sh\n")
        for number in range(2, 61):
            handle.write("echo line%d\n" % number)
    session = Session(binary, directory, command_directory, rows=20)
    try:
        session.wait_until(is_line(""))
        session.send(b"slurp slurp-tall.sh\r")
        report.record("slurp-tall-file-shows-the-rows-around-the-caret",
                      session, has_numbered_rows(46, 60))
        session.send(UP * 30)
        report.record("slurp-tall-file-follows-the-caret-up", session,
                      has_numbered_rows(30, 44))
        session.send(b"X")
        session.send(UP * 40)
        report.record("slurp-up-on-the-first-line-recalls-no-history", session,
                      lambda screen: has_numbered_rows(1, 15)(screen)
                      and has_row("#!/bin/sh")(screen))
        session.send(CTRL_X + b"s")
        report.record("slurp-tall-file-saves-every-line", session,
                      lambda screen: has_text("wrote")(screen)
                      and read_slurped(script).count("\n") == 60
                      and "echo line30X\n" in read_slurped(script))
        session.send(CTRL_X + CTRL_C)
        session.wait_until(is_line(""))

        session.send(b"slurp slurp-tall.sh\r")
        session.wait_until(has_numbered_rows(46, 60))
        session.send(UP * 31 + b"\x05; read -")
        session.wait_until(lambda screen: any(line.endswith("; read -")
                                              for line in screen.get_lines()))
        session.send(b"\t")

        def do_find_caret_row(screen):
            lines = [line.rstrip() for line in screen.get_lines()]
            rows = [index for index, line in enumerate(lines)
                    if line.endswith("; read -")]
            return (lines, rows[0]) if rows else (lines, None)

        def is_menu_over_next_row(screen):
            lines, row = do_find_caret_row(screen)
            return (row is not None and row + 1 < len(lines)
                    and "selecting completions" in lines[row + 1]
                    and "echo line43" in lines)

        report.record("slurp-menu-covers-the-rows-under-the-caret", session,
                      is_menu_over_next_row)
        session.send(ESCAPE)

        def is_next_row_restored(screen):
            lines, row = do_find_caret_row(screen)
            return (row is not None and row + 1 < len(lines)
                    and lines[row + 1] == "echo line30X")

        report.record("slurp-closed-menu-restores-the-rows", session,
                      is_next_row_restored)
        report.record("slurp-top-row-shows-the-command", session,
                      lambda screen: screen.get_lines()[0].endswith(
                          "slurp slurp-tall.sh"))
        report.record("slurp-status-rows-show-the-caret-line", session,
                      has_text("line 29 of 60, column 20, as emacs"))
        session.send(CTRL_X + b"i")
        report.record("slurp-ctrl-x-i-shows-the-hint", session,
                      lambda screen: has_text("synopsis")(screen)
                      and not has_text("line 29 of 60")(screen))
        session.send(b"\x1bx")
        report.record("slurp-command-line-keeps-the-view", session,
                      lambda screen: screen.get_lines()[1]
                      == "echo line29; read -"
                      and any(line.strip() == "M-x"
                              for line in screen.get_lines()))
        session.send(ESCAPE)
        session.wait_until(has_text("line 29 of 60"))
        session.send(b"\x1b[<0;8;4M\x1b[<0;8;4m")
        report.record("slurp-mouse-press-moves-the-caret", session,
                      has_text("line 31 of 60, column 8,"))
        session.send(b"\x1b[<65;8;4M")
        report.record("slurp-wheel-scrolls-the-view", session,
                      lambda screen: has_numbered_rows(32, 46)(screen)
                      and has_text("line 32 of 60")(screen))
        session.send(KITTY_CTRL_C)
        session.wait_until(has_text("ctrl-c again"))
        session.send(KITTY_CTRL_C)
        session.wait_until(is_line(""))
    finally:
        session.close()


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
        write_script(os.path.join(command_directory, "control-visual"),
                     "printf ': a\\033]0;PWN\\007b\\n' > \"$1\"\n")
        write_script(os.path.join(command_directory, "tty-modes"),
                     "exec '%s' -a\n" % shutil.which("stty"))
        write_script(os.path.join(command_directory, "reading-visual"),
                     "while :; do kill -TTIN $$; done\n")
        write_script(os.path.join(command_directory, "restopping-visual"),
                     "while :; do kill -STOP $$; done\n")
        run_checks(binary, directory, command_directory, report)
        run_ctrl_x_mode_checks(binary, directory, command_directory, report)
        run_slurp_checks(binary, directory, command_directory, report)
        run_tall_slurp_checks(binary, directory, command_directory, report)
        run_fatal_signal_checks(binary, directory, command_directory, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
