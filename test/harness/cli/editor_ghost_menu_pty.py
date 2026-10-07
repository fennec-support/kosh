#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives the interactive editor through a real PTY of fixed size and checks the
# ghost suggestion and the completion menu from parsed terminal state. A small
# terminal model replays the escape sequences the editor writes and tracks which
# cells are drawn in the dim ghost color. The checks cover the ghost appearing
# without Tab, its acceptance through Right, End, and Ctrl-E, menu narrowing and
# widening on every keystroke, Ctrl-W and Alt-Backspace refreshing an open menu
# down to an empty line, Escape and Ctrl-C afterwards, and session functions and
# aliases in ghost and Tab completion. A complete -C command runs once while a
# filter narrows and widens its menu, and again below the gathered token.
# complete -E serves only a line that holds nothing, and -I serves a command
# word with the caret at its start. It
# also covers word-wise ghost
# acceptance through Ctrl-Right and Alt-F, and prefix history search on Up and
# Down with its option switched off, and the inline hint rows for a command and
# a flag, their header naming the kind and the two-column indent, their absence
# inside the command word and for an uncached command, their yielding to the
# menu, their erasure on submit, and their option. A narrow terminal wraps a
# long synopsis onto several indented rows that a submit erases, and a short
# terminal keeps the input on screen with fewer rows. A pause loads
# the --help usage, flag forms, and subcommand usage of a trusted allowlisted
# command once per key and never runs one from a world-writable directory. A
# slow manpath started by a pause leaves typed keys served while it runs, and
# Tab adopts that run instead of forking another. The
# row shows an alias expansion before its target synopsis, a function
# definition, the first analysis finding of a paused line, and the command of
# the pipeline segment or command substitution under the caret. The same row
# names an unterminated quote or substitution, an open subshell, conditional,
# if, loop or function, a misplaced closing keyword, and a bad for variable,
# and it stays on the synopsis for closed text, a comment, and a trailing
# backslash. Caret moves onto matched brackets keep the line and the caret,
# so a key typed there lands in place. The auto-pair option inserts, steps
# over, and erases closers. A file name with control bytes completes in the
# $'...' form, and neither the ghost nor the menu writes those bytes raw to the
# terminal. Every wait polls for the expected final
# state under a deadline, so a failure reports the last screen instead of
# hanging. Each check prints one stable PASS line for the golden output.

import fcntl
import os
import pty
import re
import select
import signal
import shutil
import struct
import sys
import tempfile
import termios
import time


COLUMNS = 120
ROWS = 40
WAIT_SECONDS = 8.0
MENU_HEADER = "selecting completions"
MENU_FOOTER = "showing "
RIGHT = b"\x1b[C"
LEFT = b"\x1b[D"
HOME = b"\x1b[H"
END = b"\x1b[F"
UP = b"\x1b[A"
DOWN = b"\x1b[B"
CTRL_RIGHT = b"\x1b[1;5C"
ALT_F = b"\x1bf"
CTRL_A = b"\x01"
CTRL_E = b"\x05"
CTRL_W = b"\x17"
CTRL_C = b"\x03"
CTRL_D = b"\x04"
ALT_BACKSPACE = b"\x1b\x7f"
BACKSPACE = b"\x7f"
ESCAPE = b"\x1b"
CSI_PATTERN = re.compile(rb"\x1b\[([0-9;<=>?]*)([ -/]*[@-~])")


class Screen:
    def __init__(self):
        self.rows = [[]]
        self.row = 0
        self.column = 0
        self.is_dim = False
        self.pending = b""

    def get_cells(self):
        while len(self.rows) <= self.row:
            self.rows.append([])
        return self.rows[self.row]

    def put(self, text):
        cells = self.get_cells()
        while len(cells) < self.column:
            cells.append((" ", False))
        if self.column < len(cells):
            cells[self.column] = (text, self.is_dim)
        else:
            cells.append((text, self.is_dim))
        self.column += 1

    def apply_style(self, parameters):
        index = 0
        values = parameters or [0]
        while index < len(values):
            value = values[index]
            if value in (38, 48):
                self.is_dim = False
                index += 2 if index + 1 < len(values) and values[index + 1] == 5 else 4
                continue
            if value in (2, 90):
                self.is_dim = True
            elif value in (0, 22, 39) or 30 <= value <= 37 or 91 <= value <= 97:
                self.is_dim = False
            index += 1

    def apply_csi(self, parameter_text, final):
        if parameter_text[:1] in ("<", "=", ">", "?"):
            return
        parameters = [int(part) if part else 0
                      for part in parameter_text.split(";")] if parameter_text else []
        count = parameters[0] if parameters and parameters[0] else 1
        if final == "m":
            self.apply_style(parameters)
        elif final == "K":
            cells = self.get_cells()
            mode = parameters[0] if parameters else 0
            if mode == 0:
                del cells[self.column:]
            elif mode == 2:
                cells.clear()
        elif final == "J":
            del self.get_cells()[self.column:]
            del self.rows[self.row + 1:]
        elif final == "G":
            self.column = count - 1
        elif final == "A":
            self.row = max(0, self.row - count)
        elif final == "B":
            self.row += count
        elif final == "C":
            self.column += count
        elif final == "D":
            self.column = max(0, self.column - count)

    def feed(self, data):
        data = self.pending + data
        index = 0
        while index < len(data):
            byte = data[index]
            if byte == 0x1b:
                if index + 1 >= len(data):
                    break
                if data[index + 1] == 0x5b:
                    match = CSI_PATTERN.match(data, index)
                    if match is None:
                        break
                    self.apply_csi(match.group(1).decode(), match.group(2).decode())
                    index = match.end()
                    continue
                if data[index + 1] == 0x5d:
                    end = data.find(b"\x07", index)
                    if end < 0:
                        break
                    index = end + 1
                    continue
                index += 2
                continue
            if byte == 13:
                self.column = 0
            elif byte == 10:
                self.row += 1
            elif byte >= 32:
                length = 1 if byte < 0x80 else 2 if byte < 0xe0 else 3 if byte < 0xf0 else 4
                if index + length > len(data):
                    break
                self.put(data[index:index + length].decode("utf-8", "replace"))
                index += length
                continue
            index += 1
        self.pending = data[index:]

    def get_lines(self):
        return ["".join(text for text, _ in cells).rstrip() for cells in self.rows]

    def get_prompt_row(self):
        for row in range(len(self.rows) - 1, -1, -1):
            if any(text == "\u2022" for text, _ in self.rows[row]):
                return row
        return -1

    def get_typed_and_ghost(self):
        row = self.get_prompt_row()
        if row < 0:
            return None
        cells = self.rows[row]
        mark = next(index for index, (text, _) in enumerate(cells) if text == "\u2022")
        typed = "".join(text for text, is_dim in cells[mark + 2:] if not is_dim)
        ghost = "".join(text for text, is_dim in cells[mark + 2:] if is_dim)
        return typed.rstrip(), ghost.rstrip()

    def get_menu(self):
        row = self.get_prompt_row()
        if row < 0:
            return None
        lines = self.get_lines()[row + 1:]
        if not lines or MENU_HEADER not in lines[0]:
            return None
        entries = []
        total = None
        for line in lines[1:]:
            text = line.strip()
            if text.startswith(MENU_FOOTER):
                total = int(text.split(" of ")[1])
            elif text and text != "loading...":
                entries.append(text)
        return entries, total

    def count_lines(self, text):
        return sum(1 for line in self.get_lines() if line.strip() == text)

    def get_hint_rows(self):
        row = self.get_prompt_row()
        lines = self.get_lines()
        if row < 0 or row + 1 >= len(lines) or MENU_HEADER in lines[row + 1]:
            return []
        rows = []
        for line in lines[row + 1:]:
            if not line:
                break
            rows.append(line)
        return rows

    def get_hint_header(self):
        rows = self.get_hint_rows()
        return rows[0].strip() if len(rows) > 1 else ""

    def get_hint(self):
        rows = self.get_hint_rows()
        body = rows[1:] if len(rows) > 1 else rows
        return " ".join(line.strip() for line in body)


class Session:
    def __init__(self, binary, directory, command_directory, columns=COLUMNS,
                 rows=ROWS):
        environment = {
            "PATH": command_directory,
            "HOME": directory,
            "KOSH_HISTORY_FILE": os.path.join(directory, "history"),
            "TERM": "xterm-256color",
            "LANG": "C.UTF-8",
        }
        self.screen = Screen()
        self.raw = bytearray()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(directory)
            os.execve(binary, [binary, "-i", "--rcfile", "/dev/null"], environment)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ,
                    struct.pack("HHHH", rows, columns, 0, 0))
        self.is_closed = False

    def pump(self, seconds):
        ready, _, _ = select.select([self.fd], [], [], seconds)
        if not ready:
            return True
        try:
            chunk = os.read(self.fd, 65536)
        except OSError:
            return False
        if not chunk:
            return False
        self.raw.extend(chunk)
        self.screen.feed(chunk)
        return True

    def wait_until(self, is_ready):
        deadline = time.monotonic() + WAIT_SECONDS
        while time.monotonic() < deadline:
            if is_ready(self.screen):
                return True
            if not self.pump(0.02):
                return is_ready(self.screen)
        return is_ready(self.screen)

    def send(self, data):
        os.write(self.fd, data)

    def close(self):
        if self.is_closed:
            return
        self.is_closed = True
        try:
            os.kill(self.pid, signal.SIGKILL)
        except OSError:
            pass
        os.waitpid(self.pid, 0)
        os.close(self.fd)


def get_state(screen):
    return screen.get_typed_and_ghost()


def is_line(typed, ghost=""):
    return lambda screen: get_state(screen) == (typed, ghost)


def is_menu(names, total_filter=None):
    def do_check(screen):
        menu = screen.get_menu()
        if menu is None:
            return False
        entries, total = menu
        if total_filter is not None and not total_filter(total):
            return False
        return sorted(entries) == sorted(names)
    return do_check


def has_typed_menu(typed, names):
    return lambda screen: (get_state(screen) is not None
                           and get_state(screen)[0] == typed
                           and is_menu(names)(screen))


def is_menu_under_token(token):
    def do_check(screen):
        row = screen.get_prompt_row()
        lines = screen.get_lines()
        if row < 0 or row + 1 >= len(lines) or MENU_HEADER not in lines[row + 1]:
            return False
        return lines[row + 1].index(MENU_HEADER) == lines[row].index(token)
    return do_check


def is_all_commands_menu(screen):
    menu = screen.get_menu()
    return (get_state(screen) is not None and get_state(screen)[0] == ""
            and menu is not None
            and menu[1] is not None and menu[1] > 20)


def is_menu_closed(screen):
    return screen.get_menu() is None and get_state(screen) is not None


def has_hint(text):
    return lambda screen: text in screen.get_hint()


def has_hint_header(header):
    return lambda screen: screen.get_hint_header() == header


def is_hint_under(header, text):
    return lambda screen: (screen.get_hint_header() == header
                           and screen.get_hint() == text)


def are_hint_rows_indented(screen):
    rows = screen.get_hint_rows()
    return len(rows) > 1 and all(line.startswith("  ") and line[2] != " "
                                 for line in rows)


def is_without_hint(typed):
    return lambda screen: (get_state(screen) is not None
                           and get_state(screen)[0] == typed
                           and screen.get_hint() == "")


class Report:
    def __init__(self):
        self.is_ok = True

    def record(self, name, session, is_ready):
        if session.wait_until(is_ready):
            print("%s PASS" % name)
            return True
        print("%s FAIL" % name)
        sys.stderr.write("%s: typed/ghost=%r menu=%r\n%s\n" % (
            name, get_state(session.screen), session.screen.get_menu(),
            "\n".join(session.screen.get_lines()[-20:])))
        self.is_ok = False
        return False


def clear_line(session):
    session.send(CTRL_C)
    session.wait_until(is_line(""))


def type_text(session, text):
    for byte in text:
        session.send(bytes([byte]))
        session.pump(0.02)


def is_hint(text):
    return lambda screen: screen.get_hint() == text


def record_diagnostic(report, session, name, typed, expected,
                      header="syntax error"):
    type_text(session, typed)
    report.record(name, session, is_hint_under(header, expected))
    clear_line(session)


def run_command(session, report, name, keys, expected_text, expected_count):
    session.send(keys)
    session.send(b"\r")
    report.record(
        name, session,
        lambda screen: screen.count_lines(expected_text) == expected_count
        and get_state(screen) == ("", ""))


def count_marker_lines(directory, name):
    path = os.path.join(directory, name)
    if not os.path.exists(path):
        return 0
    with open(path) as handle:
        return len(handle.read().splitlines())


def run_idle_hint_checks(session, report, directory):
    session.send(b"act ")
    report.record("idle-hint-loads-help-usage", session,
                  is_hint_under("command synopsis", "act [command] [flags]"))
    session.send(b"-f")
    report.record("idle-hint-names-flag-value", session,
                  is_hint_under("flag",
                                "-f, --file=FILE: read the workflow from FILE"))
    clear_line(session)

    session.send(b"act run ")
    report.record("idle-hint-loads-subcommand-usage", session,
                  is_hint_under("subcommand synopsis", "act run [--job NAME]"))
    clear_line(session)

    session.send(b"act ")
    session.wait_until(is_hint("act [command] [flags]"))
    session.pump(0.6)
    report.record("idle-hint-loads-each-key-once", session,
                  lambda screen: count_marker_lines(directory, "act-marker")
                  == 2)
    clear_line(session)

    session.send(b"adb ")
    session.wait_until(is_line("adb"))
    session.pump(0.6)
    report.record("idle-hint-skips-untrusted-directory", session,
                  lambda screen: is_without_hint("adb")(screen)
                  and count_marker_lines(directory, "adb-marker") == 0)
    clear_line(session)

    session.send(b"alias zzcat='cat -n'\r")
    session.wait_until(is_line(""))
    session.send(b"zzcat ")
    report.record("hint-shows-alias-expansion", session,
                  lambda screen: has_hint_header("alias synopsis")(screen)
                  and screen.get_hint().startswith("zzcat='cat -n' · cat ["))
    clear_line(session)

    session.send(b"zzfunc ")
    report.record("hint-shows-function-definition", session,
                  is_hint_under("function synopsis",
                                "zzfunc () { echo FUNC-RAN; }"))
    clear_line(session)

    with open(os.path.join(directory, "zzdefs.sh"), "w") as handle:
        handle.write("true\nzzfile() { echo FILE-RAN; }\n")
    session.send(b"source ./zzdefs.sh\r")
    session.wait_until(is_line(""))
    session.send(b"zzfile ")
    report.record("hint-names-function-file", session,
                  is_hint_under("function synopsis",
                                "zzfile, defined at ./zzdefs.sh:2"))
    clear_line(session)

    session.send(b"echo $zzvalue")
    report.record("idle-hint-shows-analysis-finding", session,
                  is_hint_under("error",
                                "An unquoted variable can split into words "
                                "and expand globs. (SC2086)"))
    clear_line(session)

    session.send(b"echo hi | cat -n")
    report.record("hint-follows-pipeline-segment", session,
                  has_hint("Number every output line"))
    clear_line(session)

    session.send(b"echo $(cat -n)")
    session.wait_until(is_line("echo $(cat -n)"))
    session.send(LEFT)
    report.record("hint-follows-command-substitution", session,
                  has_hint("Number every output line"))
    clear_line(session)


def run_cached_filter_checks(session, report, directory):
    session.send(b"complete -C %s zzgen\r"
                 % os.path.join(directory, "count-words").encode())
    session.wait_until(is_line(""))
    session.send(b"zzgen a\t")
    words = ["apple", "apricot", "apron", "avocado"]
    report.record("command-spec-menu-opens", session, is_menu(words))
    for name, key, typed, expected in (
        ("command-spec-menu-narrows-first", b"p", "zzgen ap", words[:3]),
        ("command-spec-menu-narrows-second", b"r", "zzgen apr", words[1:3]),
        ("command-spec-menu-narrows-third", b"o", "zzgen apro", ["apron"]),
    ):
        session.send(key)
        report.record(name, session, has_typed_menu(typed, expected))
    report.record("command-spec-filter-runs-once", session,
                  lambda screen: count_marker_lines(directory,
                                                    "count-runs") == 1)
    session.send(BACKSPACE + BACKSPACE)
    report.record("command-spec-menu-widens-without-rerun", session,
                  lambda screen: has_typed_menu("zzgen ap", words[:3])(screen)
                  and count_marker_lines(directory, "count-runs") == 1)
    session.send(BACKSPACE + BACKSPACE)
    report.record("command-spec-menu-reruns-below-gathered-token", session,
                  lambda screen: has_typed_menu("zzgen", words + ["banana"])(
                      screen)
                  and count_marker_lines(directory, "count-runs") == 2)
    clear_line(session)

    session.send(b"complete -C %s zzgrow\r"
                 % os.path.join(directory, "grow-words").encode())
    session.wait_until(is_line(""))
    session.send(b"zzgrow t\t")
    report.record("command-spec-prefix-grows-into-menu", session,
                  has_typed_menu("zzgrow tool-", ["tool-alpha", "tool-beta"]))
    report.record("command-spec-prefix-menu-runs-once", session,
                  lambda screen: count_marker_lines(directory,
                                                    "grow-runs") == 1)
    session.send(b"a")
    report.record("command-spec-prefix-menu-narrows-without-rerun", session,
                  lambda screen: has_typed_menu("zzgrow tool-a",
                                                ["tool-alpha"])(screen)
                  and count_marker_lines(directory, "grow-runs") == 1)
    clear_line(session)

    session.send(b"complete -E -W zzempty; complete -I -W zzinitial\r")
    session.wait_until(is_line(""))
    session.send(b"ls" + CTRL_A + b"\t")
    report.record("empty-slot-needs-an-empty-line", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0].startswith("zzinitial"))
    clear_line(session)
    session.send(b"\t")
    report.record("empty-slot-serves-an-empty-line", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0].startswith("zzempty"))
    clear_line(session)
    session.send(b"complete -r -E; complete -r -I\r")
    session.wait_until(is_line(""))


def run_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory)
    try:
        if not report.record("startup-prompt", session, is_line("")):
            return

        session.send(b"cat sub/al")
        report.record("ghost-without-tab", session,
                      is_line("cat sub/al", "pha-beta.txt"))
        session.send(RIGHT)
        report.record("ghost-right-accepts", session,
                      is_line("cat sub/alpha-beta.txt"))
        run_command(session, report, "ghost-right-runs", b"",
                    "ALPHA-CONTENT", 1)

        session.send(b"cat sub/al")
        session.wait_until(is_line("cat sub/al", "pha-beta.txt"))
        session.send(END)
        report.record("ghost-end-accepts", session,
                      is_line("cat sub/alpha-beta.txt"))
        run_command(session, report, "ghost-end-runs", b"",
                    "ALPHA-CONTENT", 2)

        session.send(b"cat sub/al")
        session.wait_until(is_line("cat sub/al", "pha-beta.txt"))
        session.send(CTRL_E)
        report.record("ghost-ctrl-e-accepts", session,
                      is_line("cat sub/alpha-beta.txt"))
        run_command(session, report, "ghost-ctrl-e-runs", b"",
                    "ALPHA-CONTENT", 3)

        session.send(b"cat menu/menu-")
        session.send(b"\t")
        names = ["menu/menu-apple", "menu/menu-apricot",
                 "menu/menu-avocado", "menu/menu-banana"]
        report.record("menu-opens-with-all-candidates", session,
                      is_menu(names, lambda total: total in (None, 4)))
        session.send(b"a")
        report.record("menu-narrows-on-first-letter", session,
                      is_menu(names[:3]))
        session.send(b"p")
        report.record("menu-narrows-on-second-letter", session,
                      is_menu(names[:2]))
        session.send(BACKSPACE)
        report.record("menu-widens-on-backspace", session, is_menu(names[:3]))
        session.send(BACKSPACE)
        report.record("menu-widens-to-all-on-backspace", session, is_menu(names))
        session.send(ESCAPE)
        report.record("menu-escape-closes", session, is_menu_closed)
        clear_line(session)

        session.send(b"cat menu/m\t")
        report.record("common-prefix-tab-opens-menu", session,
                      has_typed_menu("cat menu/menu-", names))
        report.record("menu-starts-under-the-token", session,
                      is_menu_under_token("menu/menu-"))
        session.send(ESCAPE)
        session.wait_until(is_menu_closed)
        clear_line(session)

        session.send(b"cat menu/menu-ap")
        session.send(b"\t")
        session.wait_until(is_menu(names[:2]))
        for name, keys, typed, names_expected in (
            ("menu-ctrl-w-drops-partial-word", CTRL_W, "cat menu/menu-", names),
            ("menu-alt-backspace-drops-dash", ALT_BACKSPACE, "cat menu/menu", names),
            ("menu-alt-backspace-drops-name", ALT_BACKSPACE, "cat menu/", names),
            ("menu-alt-backspace-drops-slash", ALT_BACKSPACE, "cat menu", ["menu/"]),
        ):
            session.send(keys)
            report.record(name, session, has_typed_menu(typed, names_expected))
        session.send(ALT_BACKSPACE)
        report.record("menu-alt-backspace-reaches-argument-position", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0] == "cat"
                      and screen.get_menu() is not None
                      and "menu/" in screen.get_menu()[0]
                      and "sub/" in screen.get_menu()[0])
        session.send(CTRL_W)
        report.record("menu-ctrl-w-reaches-command-word", session,
                      has_typed_menu("cat", ["cat"]))
        session.send(CTRL_W)
        report.record("menu-ctrl-w-reaches-empty-line", session,
                      is_all_commands_menu)
        session.send(ESCAPE)
        report.record("menu-escape-after-empty-line", session,
                      lambda screen: is_menu_closed(screen)
                      and get_state(screen) == ("", ""))
        run_command(session, report, "shell-alive-after-escape",
                    b"echo STILL-ALIVE", "STILL-ALIVE", 1)

        session.send(b"zzprobe-\t")
        report.record("menu-lists-path-commands", session,
                      is_menu(["zzprobe-one", "zzprobe-two"]))
        session.send(ALT_BACKSPACE)
        report.record("menu-alt-backspace-refreshes-command", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0] == "zzprobe"
                      and screen.get_menu() is not None
                      and "zzprobe-one" in screen.get_menu()[0])
        session.send(ALT_BACKSPACE)
        report.record("menu-alt-backspace-reaches-empty-line", session,
                      is_all_commands_menu)
        session.send(CTRL_C)
        report.record("menu-ctrl-c-closes", session,
                      lambda screen: is_menu_closed(screen)
                      and get_state(screen) == ("", ""))
        run_command(session, report, "shell-alive-after-ctrl-c",
                    b"echo STILL-ALIVE", "STILL-ALIVE", 2)

        session.send(b"zzfunc() { echo FUNC-RAN; }\r")
        session.wait_until(is_line(""))
        session.send(b"alias zzalias='echo ALIAS-RAN'\r")
        session.wait_until(is_line(""))
        session.send(b"zzfu")
        report.record("function-in-ghost", session, is_line("zzfu", "nc"))
        session.send(RIGHT)
        run_command(session, report, "function-ghost-runs", b"",
                    "FUNC-RAN", 1)
        session.send(b"zzal")
        report.record("alias-in-ghost", session, is_line("zzal", "ias"))
        session.send(END)
        run_command(session, report, "alias-ghost-runs", b"", "ALIAS-RAN", 1)

        session.send(b"zz\t")
        report.record("function-alias-in-tab-menu", session,
                      is_menu(["zzalias", "zzfunc", "zzprobe-one",
                               "zzprobe-two"]))
        session.send(b"f")
        report.record("function-narrowed-in-tab-menu", session,
                      is_menu(["zzfunc"]))
        clear_line(session)

        run_cached_filter_checks(session, report, directory)

        run_command(session, report, "history-seed-alpha",
                    b"echo hist-alpha", "hist-alpha", 1)
        run_command(session, report, "history-seed-beta",
                    b"echo hist-beta word", "hist-beta word", 1)
        run_command(session, report, "history-seed-gamma",
                    b"echo hist-gamma", "hist-gamma", 1)
        run_command(session, report, "history-seed-true", b"true", "true", 0)

        session.send(b"echo hist-b")
        report.record("word-ghost-shown", session,
                      is_line("echo hist-b", "eta word"))
        session.send(CTRL_RIGHT)
        report.record("ctrl-right-accepts-one-word", session,
                      is_line("echo hist-beta", " word"))
        session.send(ALT_F)
        report.record("alt-f-accepts-next-word", session,
                      is_line("echo hist-beta word"))
        clear_line(session)

        session.send(b"echo hist-b")
        session.wait_until(is_line("echo hist-b", "eta word"))
        session.send(RIGHT)
        report.record("right-still-accepts-whole-ghost", session,
                      is_line("echo hist-beta word"))
        clear_line(session)

        session.send(b"echo hist-")
        session.wait_until(is_line("echo hist-", "gamma"))
        session.send(UP)
        report.record("prefix-up-recalls-newest-match", session,
                      is_line("echo hist-gamma"))
        session.send(UP)
        report.record("prefix-up-skips-other-commands", session,
                      is_line("echo hist-beta word"))
        session.send(UP)
        report.record("prefix-up-reaches-oldest-match", session,
                      is_line("echo hist-alpha"))
        session.send(UP)
        report.record("prefix-up-stays-at-oldest-match", session,
                      is_line("echo hist-alpha"))
        session.send(DOWN)
        report.record("prefix-down-walks-back", session,
                      is_line("echo hist-beta word"))
        session.send(DOWN)
        session.send(DOWN)
        report.record("prefix-down-restores-typed-text", session,
                      is_line("echo hist-"))
        clear_line(session)

        session.send(UP)
        report.record("empty-line-up-keeps-plain-history", session,
                      is_line("true"))
        clear_line(session)

        session.send(b"koshconf set history.prefix_search off\r")
        session.wait_until(is_line(""))
        session.send(b"echo hist-")
        session.wait_until(is_line("echo hist-", "gamma"))
        session.send(UP)
        report.record("option-off-up-recalls-newest-entry", session,
                      is_line("koshconf set history.prefix_search off"))
        clear_line(session)

        session.send(b"cat ")
        report.record("hint-shows-command-synopsis", session,
                      has_hint("cat ["))
        report.record("hint-names-utility-header", session,
                      is_hint_under("utility synopsis",
                                    "cat [-nu] [--syntax-highlighting] "
                                    "[file ...]"))
        report.record("hint-rows-indented", session, are_hint_rows_indented)
        session.send(b"-n")
        report.record("hint-shows-flag-description", session,
                      has_hint("Number every output line"))
        report.record("hint-names-flag-header", session,
                      lambda screen: has_hint_header("flag")(screen)
                      and screen.get_hint().startswith("-n"))
        session.send(BACKSPACE * 3)
        report.record("hint-clears-inside-command-word", session,
                      is_without_hint("cat"))
        clear_line(session)

        session.send(b"echo ")
        report.record("hint-names-builtin-header", session,
                      is_hint_under("builtin synopsis", "echo [-neE] [arg ...]"))
        clear_line(session)

        session.send(b"zzprobe-one ")
        session.wait_until(is_line("zzprobe-one"))
        session.pump(0.3)
        report.record("hint-absent-for-uncached-command", session,
                      is_without_hint("zzprobe-one"))
        clear_line(session)

        session.send(b"cat menu/menu-")
        session.wait_until(has_hint("cat ["))
        session.send(b"\t")
        report.record("hint-yields-to-menu", session, is_menu(names))
        session.send(ESCAPE)
        report.record("hint-returns-after-menu-closes", session,
                      lambda screen: is_menu_closed(screen)
                      and "cat [" in screen.get_hint())
        clear_line(session)

        session.send(b"cat -n sub/alpha-beta.txt")
        session.wait_until(has_hint("Number every output line"))
        session.send(b"\r")
        report.record("hint-erased-on-submit", session,
                      lambda screen: get_state(screen) == ("", "")
                      and any("ALPHA-CONTENT" in line
                              for line in screen.get_lines()[:-1])
                      and not any("Number every" in line
                                  or line.strip() == "flag"
                                  for line in screen.get_lines()))

        run_idle_hint_checks(session, report, directory)

        record_diagnostic(report, session, "diagnostic-double-quote",
                          b'echo "abc',
                          'Unterminated string literal, expected " here')
        record_diagnostic(report, session, "diagnostic-single-quote",
                          b"echo 'abc",
                          "Unterminated string literal, expected ' here")
        record_diagnostic(report, session, "diagnostic-ansi-c-quote",
                          b"echo $'abc",
                          "Unterminated $'...' string, expected ' here")
        record_diagnostic(report, session, "diagnostic-command-substitution",
                          b"echo $(ls",
                          "Unterminated command substitution, expected ) here")
        record_diagnostic(report, session, "diagnostic-arithmetic",
                          b"echo $((1+",
                          "Unterminated arithmetic expansion, expected )) here")
        record_diagnostic(report, session, "diagnostic-backtick",
                          b"echo `ls",
                          "Unterminated command substitution, expected ` here")
        record_diagnostic(report, session, "diagnostic-subshell",
                          b"(echo hi", "Unterminated subshell, expected ')'")
        record_diagnostic(report, session, "diagnostic-conditional",
                          b"[[ a == b",
                          "Unterminated '[[', expected ']]'")
        record_diagnostic(report, session, "diagnostic-if-condition",
                          b"if true",
                          "Unterminated if, expected 'then' after the "
                          "condition")
        record_diagnostic(report, session, "diagnostic-if-body",
                          b"if true; then echo hi",
                          "Unterminated if, expected 'fi'")
        record_diagnostic(report, session, "diagnostic-loop-body",
                          b"while true; do :",
                          "Unterminated loop, expected 'done'")
        record_diagnostic(report, session, "diagnostic-empty-loop-open",
                          b"for x in a; do ",
                          "Unterminated for loop, expected 'done'")
        record_diagnostic(report, session, "diagnostic-nested-construct",
                          b"echo $(if true; then",
                          "Unterminated command substitution, expected ) here")
        record_diagnostic(report, session, "diagnostic-bad-for-variable",
                          b"for 1x in a; do",
                          "Bad for loop variable, '1x' is not a plain name, "
                          "drop the '$' and any quotes")
        record_diagnostic(report, session, "diagnostic-process-substitution",
                          b"cat <(ls",
                          "Unterminated process substitution, expected ) "
                          "here")
        record_diagnostic(report, session, "diagnostic-brace-group",
                          b"f() {", "Unterminated brace group, expected '}'")
        record_diagnostic(report, session, "diagnostic-trailing-pipe",
                          b"ls |",
                          "Unable to build the pipeline because no command "
                          "follows the pipe to receive the output")
        record_diagnostic(report, session, "diagnostic-trailing-and",
                          b"ls &&", "Expected a command after an operator")
        record_diagnostic(report, session, "diagnostic-leading-pipe",
                          b"| ls", "Expected a command before the pipe")
        record_diagnostic(report, session, "diagnostic-redirection-target",
                          b"echo >", "Expected a filename after the redir")
        record_diagnostic(report, session, "diagnostic-heredoc-delimiter",
                          b"cat <<", "Expected a heredoc delimiter")
        record_diagnostic(report, session, "diagnostic-case-without-in",
                          b"case x ",
                          "Expected an unquoted 'in' after the case word")
        record_diagnostic(report, session, "diagnostic-function-name",
                          b"function ",
                          "Expected a name after the 'function' keyword")
        record_diagnostic(report, session, "diagnostic-stray-paren",
                          b"echo )", "')' has no matching '('")
        record_diagnostic(report, session, "diagnostic-stray-case-terminator",
                          b"echo ;;",
                          "';;' is only valid between the arms of a 'case'")
        record_diagnostic(report, session, "diagnostic-if-subshell-condition",
                          b"if (true)",
                          "Unterminated if, expected 'then' after the "
                          "condition")
        record_diagnostic(report, session, "diagnostic-empty-subshell",
                          b"if ( )",
                          "Unable to parse the subshell, the body between "
                          "'(' and ')' is empty, a command is required")
        record_diagnostic(report, session, "diagnostic-empty-brace-group",
                          b"f() { }",
                          "Unable to parse the brace group, the body between "
                          "'{' and '}' is empty, a command is required")

        type_text(session, b"echo hi; fi")
        report.record("diagnostic-shown-at-word-end", session,
                      is_hint("'fi' has no matching 'if'"))
        session.send(b" ")
        report.record("diagnostic-shown-after-word", session,
                      is_hint("'fi' has no matching 'if'"))
        clear_line(session)
        record_diagnostic(report, session, "diagnostic-stray-closer",
                          b"echo hi; fi ",
                          "'fi' has no matching 'if'")
        record_diagnostic(report, session, "diagnostic-stray-done",
                          b"if true; then echo; done ",
                          "'done' has no matching 'while', 'until', or 'for'")
        record_diagnostic(report, session, "diagnostic-absent-when-closed",
                          b'echo "abc" "$(ls)" "${HOME}"',
                          "echo [-neE] [arg ...]", "builtin synopsis")
        record_diagnostic(report, session, "diagnostic-absent-in-comment",
                          b'echo hi # "abc', "echo [-neE] [arg ...]",
                          "builtin synopsis")
        record_diagnostic(report, session, "diagnostic-absent-after-backslash",
                          b"echo \\", "echo [-neE] [arg ...]",
                          "builtin synopsis")
        record_diagnostic(report, session,
                          "diagnostic-absent-after-escaped-quote",
                          b'echo \\"abc', "echo [-neE] [arg ...]",
                          "builtin synopsis")

        type_text(session, b'echo "abc')
        session.wait_until(
            is_hint('Unterminated string literal, expected " here'))
        session.send(b'"')
        report.record("diagnostic-clears-when-quote-closes", session,
                      is_hint_under("builtin synopsis",
                                    "echo [-neE] [arg ...]"))
        clear_line(session)

        type_text(session, b"echo $((1+(2*3)))")
        session.wait_until(is_line("echo $((1+(2*3)))"))
        for _ in range(3):
            session.send(LEFT)
            session.pump(0.05)
        report.record("bracket-caret-move-keeps-line", session,
                      is_line("echo $((1+(2*3)))"))
        session.send(b"0")
        report.record("bracket-caret-insert-lands", session,
                      is_line("echo $((1+(2*30)))"))
        session.send(HOME)
        session.pump(0.05)
        session.send(END)
        report.record("bracket-caret-home-end-keeps-line", session,
                      is_line("echo $((1+(2*30)))"))
        run_command(session, report, "bracket-caret-line-runs", b"", "61", 1)

        type_text(session, b"echo (")
        report.record("auto-pair-off-by-default", session, is_line("echo ("))
        clear_line(session)
        session.send(b"koshconf set editor.auto_pair on\r")
        session.wait_until(is_line(""))
        type_text(session, b"echo (")
        report.record("auto-pair-inserts-closer", session, is_line("echo ()"))
        type_text(session, b"a)")
        report.record("auto-pair-steps-over-closer", session,
                      is_line("echo (a)"))
        type_text(session, b" [")
        session.wait_until(is_line("echo (a) []"))
        session.send(BACKSPACE)
        report.record("auto-pair-backspace-deletes-pair", session,
                      is_line("echo (a)"))
        type_text(session, b"don'")
        report.record("auto-pair-quote-after-word-stays-single", session,
                      is_line("echo (a) don'"))
        clear_line(session)
        session.send(b"koshconf set editor.auto_pair off\r")
        session.wait_until(is_line(""))

        session.send(b"koshconf set editor.hints off\r")
        session.wait_until(is_line(""))
        session.send(b"cat ")
        session.wait_until(is_line("cat"))
        session.pump(0.3)
        report.record("option-off-hides-hint", session, is_without_hint("cat"))
        clear_line(session)

        type_text(session, b'echo "abc')
        report.record("hints-off-keeps-diagnostic", session,
                      has_hint("Unterminated"))
        clear_line(session)

        session.send(b"koshconf set editor.diagnostics off\r")
        session.wait_until(is_line(""))
        type_text(session, b'echo "abc')
        session.wait_until(is_line('echo "abc'))
        session.pump(0.3)
        report.record("option-off-hides-diagnostic", session,
                      is_without_hint('echo "abc'))
        clear_line(session)

        session.send(b"koshconf set editor.hints on\r")
        session.wait_until(is_line(""))
        type_text(session, b'cat "abc')
        session.pump(0.3)
        report.record("diagnostics-off-keeps-hint", session,
                      lambda screen: screen.get_hint() != ""
                      and "Unterminated" not in screen.get_hint())
        clear_line(session)

        session.send(CTRL_D)
        try:
            deadline = time.monotonic() + WAIT_SECONDS
            while time.monotonic() < deadline:
                finished, _ = os.waitpid(session.pid, os.WNOHANG)
                if finished:
                    session.is_closed = True
                    os.close(session.fd)
                    break
                session.pump(0.02)
        except OSError:
            pass
    finally:
        session.close()


def get_help_rows(screen):
    row = screen.get_prompt_row()
    if row < 0:
        return None
    rows = []
    for line in screen.get_lines()[row + 1:]:
        if line.strip().startswith("menu/"):
            return rows
        rows.append(line)
    return None


def is_help_wrapped(columns):
    def do_check(screen):
        rows = get_help_rows(screen)
        if rows is None or len(rows) < 2 or MENU_HEADER not in rows[0]:
            return False
        if any(len(line) >= columns or line.strip().startswith(",") for line in rows):
            return False
        words = " ".join(line.strip() for line in rows)
        return words == ("selecting completions, enter to run, tab to accept, "
                         "esc to close, ctrl-g to restore")
    return do_check


LS_SYNOPSIS = ("ls [-aA1dgFhklnoprRSt] [-L level] [--tree] "
               "[--one-file-system] [path ...]")


def is_hint_wrapped(columns, header, text):
    def do_check(screen):
        rows = screen.get_hint_rows()
        return (len(rows) > 2 and screen.get_hint_header() == header
                and screen.get_hint() == text
                and all(len(line) < columns for line in rows))
    return do_check


def run_narrow_checks(binary, directory, command_directory, report):
    columns = 60
    session = Session(binary, directory, command_directory, columns)
    try:
        if not report.record("narrow-startup-prompt", session, is_line("")):
            return

        session.send(b"cat menu/m\t")
        report.record("narrow-menu-help-wraps-between-items", session,
                      is_help_wrapped(columns))
        session.send(ESCAPE)
        report.record("narrow-menu-escape-closes", session,
                      lambda screen: screen.get_menu() is None
                      and get_help_rows(screen) is None)
        clear_line(session)

        session.send(b"ls ")
        report.record("narrow-hint-wraps-long-synopsis", session,
                      is_hint_wrapped(columns, "utility synopsis",
                                      LS_SYNOPSIS))
        report.record("narrow-hint-rows-indented", session,
                      are_hint_rows_indented)
        session.send(b"-d sub\r")
        report.record("narrow-hint-rows-erased-on-submit", session,
                      lambda screen: get_state(screen) == ("", "")
                      and screen.get_hint_rows() == []
                      and not any("[--one-file-system]" in line
                                  or line.strip() == "utility synopsis"
                                  for line in screen.get_lines()))
    finally:
        session.close()


def run_short_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory, 60, 3)
    try:
        if not report.record("short-startup-prompt", session, is_line("")):
            return

        session.send(b"ls ")
        report.record("short-hint-keeps-the-input-on-screen", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0] == "ls"
                      and len(screen.get_hint_rows()) == 2
                      and screen.get_hint_header() == "utility synopsis"
                      and screen.get_hint().endswith("..."))
    finally:
        session.close()


MANPATH_PROBE = """#!/bin/sh
echo $$ >> '%s'
attempt_count=0
while [ ! -f '%s' ] && [ "$attempt_count" -lt 200 ]; do
  '%s' 0.05
  attempt_count=$((attempt_count + 1))
done
echo '%s'
"""


def is_process_alive(pid):
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def run_idle_manpath_checks(binary, directory, report):
    command_directory = os.path.join(directory, "manpath-bin")
    man_root = os.path.join(directory, "manpath-root")
    marker = os.path.join(directory, "manpath-marker")
    release = os.path.join(directory, "manpath-release")
    os.makedirs(command_directory)
    os.makedirs(os.path.join(man_root, "man1"))
    probe = os.path.join(command_directory, "koshmanprobe")
    with open(probe, "w") as handle:
        handle.write("#!/bin/sh\n")
    os.chmod(probe, 0o755)
    manpath = os.path.join(command_directory, "manpath")
    with open(manpath, "w") as handle:
        handle.write(MANPATH_PROBE % (marker, release, shutil.which("sleep"),
                                      man_root))
    os.chmod(manpath, 0o755)
    with open(os.path.join(man_root, "man1", "koshmanprobe.1"), "w") as handle:
        handle.write(".TH KOSHMANPROBE 1\n.SH SYNOPSIS\n\\fBkoshmanprobe\\fR\n")
    with open(os.path.join(man_root, "man1", "koshmanprobe-recovered.1"),
              "w") as handle:
        handle.write(".TH KOSHMANPROBE-RECOVERED 1\n.SH SYNOPSIS\n"
                     "\\fBkoshmanprobe\\fR \\fBrecovered\\fR\n")

    session = Session(binary, directory, command_directory)
    try:
        if not report.record("manpath-startup-prompt", session, is_line("")):
            return

        type_text(session, b"koshmanprobe sub ")
        if not report.record("idle-manpath-load-starts", session,
                             lambda screen: count_marker_lines(
                                 directory, "manpath-marker") == 1):
            return

        with open(marker) as handle:
            pid = int(handle.read().split()[0])
        session.send(b"Q")
        report.record("idle-manpath-load-serves-keys", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0] == "koshmanprobe sub Q"
                      and is_process_alive(pid))

        open(release, "w").close()
        session.send(b"\x15koshmanprobe rec\t")
        report.record("completion-adopts-idle-manpath-load", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0].startswith(
                          "koshmanprobe recovered")
                      and count_marker_lines(directory, "manpath-marker")
                      == 1)
    finally:
        session.close()


HELP_PROBE ="""#!/bin/sh
echo forked >> '%s'
if [ "$1" = run ]; then
  echo "Usage: act run [--job NAME]"
  echo "  -j, --job=NAME   the job to run"
  exit 0
fi
echo "Usage: act [command] [flags]"
echo
echo "Commands:"
echo "  run      run a workflow"
echo
echo "Flags:"
echo "  -f, --file=FILE   read the workflow from FILE"
"""


def write_help_probe(path, marker):
    with open(path, "w") as handle:
        handle.write(HELP_PROBE % marker)
    os.chmod(path, 0o755)


COUNTING_WORDS = """#!/bin/sh
echo run >> '%s'
for word in %s; do
  case $word in "$2"*) echo "$word" ;; esac
done
"""


def write_counting_words(path, counter, words):
    with open(path, "w") as handle:
        handle.write(COUNTING_WORDS % (counter, words))
    os.chmod(path, 0o755)


def has_raw_control_name(session, mark):
    written = bytes(session.raw[mark:])
    return b"PWN\x07" in written or b"\xc2\x9b" in written


def run_control_name_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory)
    try:
        if not report.record("control-startup-prompt", session, is_line("")):
            return

        mark = len(session.raw)
        session.send(b"cat ctl/ZQa")
        session.send(b"\t")
        report.record("control-name-completes-ansi-c-quoted", session,
                      is_line("cat ctl/$'ZQa\\e]0;PWN\\ax'"))
        clear_line(session)

        session.send(b"cat ctl/ZQ")
        session.send(b"\t")
        report.record("control-name-menu-opens", session,
                      lambda screen: screen.get_menu() is not None
                      and len(screen.get_menu()[0]) == 2)
        session.send(ESCAPE)
        session.wait_until(is_menu_closed)
        clear_line(session)
        report.record("control-name-never-reaches-the-terminal-raw", session,
                      lambda screen: not has_raw_control_name(session, mark))
    finally:
        session.close()


def main():
    if sys.platform != "linux":
        print("editor ghost and menu PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)

    directory = tempfile.mkdtemp(prefix="kosh-editor-pty-")
    report = Report()
    try:
        os.makedirs(os.path.join(directory, "sub"))
        os.makedirs(os.path.join(directory, "menu"))
        os.makedirs(os.path.join(directory, "bin"))
        with open(os.path.join(directory, "sub", "alpha-beta.txt"), "w") as handle:
            handle.write("ALPHA-CONTENT\n")
        for name in ("apple", "apricot", "avocado", "banana"):
            open(os.path.join(directory, "menu", "menu-" + name), "w").close()
        for name in ("zzprobe-one", "zzprobe-two"):
            path = os.path.join(directory, "bin", name)
            with open(path, "w") as handle:
                handle.write("#!/bin/sh\n")
            os.chmod(path, 0o755)
        open_directory = os.path.join(directory, "open")
        os.makedirs(open_directory)
        os.chmod(open_directory, 0o777)
        write_counting_words(os.path.join(directory, "count-words"),
                             os.path.join(directory, "count-runs"),
                             "apple apricot apron avocado banana")
        write_counting_words(os.path.join(directory, "grow-words"),
                             os.path.join(directory, "grow-runs"),
                             "tool-alpha tool-beta other")
        write_help_probe(os.path.join(directory, "bin", "act"),
                         os.path.join(directory, "act-marker"))
        write_help_probe(os.path.join(open_directory, "adb"),
                         os.path.join(directory, "adb-marker"))
        run_checks(binary, directory,
                   os.path.join(directory, "bin") + os.pathsep + open_directory,
                   report)
        run_narrow_checks(binary, directory, os.path.join(directory, "bin"),
                          report)
        run_short_checks(binary, directory, os.path.join(directory, "bin"),
                         report)
        run_idle_manpath_checks(binary, directory, report)
        control_directory = os.path.join(directory.encode(), b"ctl")
        os.makedirs(control_directory)
        for name in (b"ZQa\x1b]0;PWN\x07x", b"ZQb\xc2\x9by"):
            open(os.path.join(control_directory, name), "w").close()
        run_control_name_checks(binary, directory,
                                os.path.join(directory, "bin"), report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
