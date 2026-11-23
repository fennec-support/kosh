#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives the interactive editor through a real PTY of fixed size and checks the
# ghost suggestion and the completion menu from parsed terminal state. A small
# terminal model replays the escape sequences the editor writes and tracks which
# cells are drawn in the dim ghost color. The checks cover the ghost appearing
# without Tab, its acceptance through Right, End, and Ctrl-E, a cd operand
# found under CDPATH once a pause has indexed it, a file a background job
# creates after the prompt reaching Tab, an empty Enter refreshing the
# listings the ghost shows, menu narrowing and
# widening on every keystroke, Ctrl-W and Alt-Backspace refreshing an open menu
# down to an empty line, Escape and Ctrl-C afterwards, and session functions and
# aliases in ghost and Tab completion. A complete -C command runs once while a
# filter narrows and widens its menu, and again below the gathered token. A
# -C reply the shell does not rank narrows to every row the token opens in
# either case. complete -E serves only a line that holds nothing, runs once,
# and keeps its own list after Tab inserts its common prefix. A fresh -I
# completion at the start of a command inserts before it. Other explicit
# completion matches text on both sides of the caret and replaces the whole
# word. With the space-after option on, a spec completion takes
# a space unless the spec has nospace, and a nosort reply keeps its order in
# the menu. Fresh Tab completes a sole path without a space and opens the
# menu. Tab in the open menu still appends a space when configured, and
# Ctrl-Z undoes the accepted file and its space. It also covers word-wise ghost
# acceptance through Ctrl-Right and Alt-F, and prefix history search on Up and
# Down with its option switched off, a Ctrl-R menu that flags a miss and cuts
# a long entry with an ellipsis, and the inline synopsis rows for a command and
# the flag under the caret, a word without a description left out, a koshconf
# option that Tab finds by subsequence, an open menu that finds one after a
# byte that matched nothing, their synopsis header and the
# two-column indent, their absence inside the command word, for an uncached
# command, and for a bundled utility that a PATH program shadows unless the
# word follows koshkit, their yielding to the menu, their erasure on submit,
# and their option. A path menu lists the last component of each path. A
# narrow terminal keeps every synopsis row within its width and a submit erases
# them, wraps the menu help without losing a word, starts the menu at the left
# edge when the token is too far
# right for its help text to keep two rows, and a short
# terminal keeps the input on screen with fewer rows. A pause loads
# the --help usage, flag forms, and subcommand usage of a trusted allowlisted
# command once per key and never runs one from a world-writable directory. A
# slow manpath started by a pause leaves typed keys served while it runs, and
# Tab adopts that run instead of forking another. The
# row shows the command of the command substitution under the caret, flags an
# unterminated double quote and returns to the synopsis rows once the quote
# closes,
# and reports a misplaced closing keyword after the word and at its end.
# Caret moves onto matched brackets keep the line and the caret,
# so a key typed there lands in place. The auto-pair option inserts a
# closer, steps over it, erases an empty pair on Backspace, keeps a quote after
# a word single, leaves brackets and quotes plain inside quoted text, pairs $(
# and ${ inside a double quote it closed itself, and types a case pattern end
# before the closer of its subshell while a parenthesized pattern steps over
# its own. A file name with control bytes completes in the
# $'...' form, and neither the ghost nor the menu writes those bytes raw to the
# terminal. A koshconf option menu keeps narrowing when the first letter typed
# after Tab matches nothing alone, as q or a does. Every wait polls for the
# expected final
# state under a deadline, so a failure reports the last screen instead of
# hanging. Each check prints one stable PASS line for the golden output.

import fcntl
import os
import pty
import re
import select
import shlex
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
FUZZY_PATH_HEADER = "fuzzy path search"
MENU_HELP = ("selecting completions, enter to run, tab to accept, esc to close, "
             "ctrl-g to restore")
MENU_FOOTER = "showing "
HISTORY_HEADER = "incremental history search"
HISTORY_NO_MATCH = "no matches, backspace to widen the search"
RIGHT = b"\x1b[C"
LEFT = b"\x1b[D"
HOME = b"\x1b[H"
END = b"\x1b[F"
UP = b"\x1b[A"
DOWN = b"\x1b[B"
CTRL_RIGHT = b"\x1b[1;5C"
CTRL_LEFT = b"\x1b[1;5D"
CTRL_ENTER_KITTY = b"\x1b[13;5u"
CTRL_ENTER_XTERM = b"\x1b[27;5;13~"
ALT_F = b"\x1bf"
CTRL_A = b"\x01"
CTRL_E = b"\x05"
CTRL_R = b"\x12"
CTRL_W = b"\x17"
CTRL_Z = b"\x1a"
CTRL_C = b"\x03"
CTRL_D = b"\x04"
ALT_BACKSPACE = b"\x1b\x7f"
BACKSPACE = b"\x7f"
DELETE = b"\x1b[3~"
ESCAPE = b"\x1b"
CSI_PATTERN = re.compile(rb"\x1b\[([0-9;<=>?]*)([ -/]*[@-~])")


class Screen:
    def __init__(self, height=ROWS):
        self.height = height
        self.rows = [[]]
        self.row = 0
        self.column = 0
        self.is_dim = False
        self.is_bold = False
        self.foreground = None
        self.background = None
        self.pending = b""
        self.saved_screen = None
        self.saved_cursor = (0, 0)
        self.margins = None

    def get_cells(self):
        while len(self.rows) <= self.row:
            self.rows.append([])
        return self.rows[self.row]

    def put(self, text):
        cells = self.get_cells()
        while len(cells) < self.column:
            cells.append((" ", False, False, None, None))
        cell = (text, self.is_dim, self.is_bold,
                self.foreground, self.background)
        if self.column < len(cells):
            cells[self.column] = cell
        else:
            cells.append(cell)
        self.column += 1

    def apply_style(self, parameters):
        index = 0
        values = parameters or [0]
        while index < len(values):
            value = values[index]
            if value in (38, 48):
                self.is_dim = False
                end = index + (3 if values[index + 1:index + 2] == [5] else 5)
                color = tuple(values[index + 1:end])
                if value == 38:
                    self.foreground = color
                else:
                    self.background = color
                index = end
                continue
            if value == 1:
                self.is_bold = True
            elif value in (0, 22):
                self.is_bold = False
            if value in (0, 39):
                self.foreground = None
            elif 30 <= value <= 37 or 90 <= value <= 97:
                self.foreground = (value,)
            if value in (0, 49):
                self.background = None
            elif 40 <= value <= 47 or 100 <= value <= 107:
                self.background = (value,)
            if value in (2, 90):
                self.is_dim = True
            elif value in (0, 22, 39) or 30 <= value <= 37 or 91 <= value <= 97:
                self.is_dim = False
            index += 1

    def get_top_row(self):
        return max(0, len(self.rows) - self.height)

    def get_margin_rows(self):
        top = self.get_top_row()
        first, last = self.margins or (1, self.height)
        while len(self.rows) < top + last:
            self.rows.append([])
        return top + first - 1, top + last - 1

    def index(self):
        first, last = self.get_margin_rows()
        if self.row == last:
            del self.rows[first]
            self.rows.insert(last, [])
        else:
            self.row += 1

    def reverse_index(self):
        first, last = self.get_margin_rows()
        if self.row == first:
            del self.rows[last]
            self.rows.insert(first, [])
        else:
            self.row = max(0, self.row - 1)

    def apply_private_csi(self, parameter_text, final):
        if parameter_text != "?1049":
            return
        if final == "h" and self.saved_screen is None:
            self.saved_screen = (self.rows, self.row, self.column)
            self.rows = [[]]
            self.row = 0
            self.column = 0
        elif final == "l" and self.saved_screen is not None:
            self.rows, self.row, self.column = self.saved_screen
            self.saved_screen = None

    def apply_csi(self, parameter_text, final):
        if parameter_text[:1] == "?":
            self.apply_private_csi(parameter_text, final)
            return
        if parameter_text[:1] in ("<", "=", ">"):
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
        elif final == "H":
            row = parameters[0] if parameters and parameters[0] else 1
            column = (parameters[1] if len(parameters) > 1 and parameters[1]
                      else 1)
            self.row = self.get_top_row() + row - 1
            self.column = column - 1
        elif final == "A":
            self.row = max(0, self.row - count)
        elif final == "B":
            self.row += count
        elif final == "C":
            self.column += count
        elif final == "D":
            self.column = max(0, self.column - count)
        elif final == "r":
            self.margins = ((parameters[0] or 1, parameters[1] or self.height)
                            if len(parameters) == 2 else None)
            self.row = self.get_top_row()
            self.column = 0

    # e.g. feed(b"ab\x1b[") draws "ab" at column 2 and keeps b"\x1b[" pending.
    # feed(b"1DX") completes cursor-left and replaces "b" with "X".
    # the row becomes "aX", and the column is 2.
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
                if data[index + 1] == ord("D"):
                    self.index()
                elif data[index + 1] == ord("M"):
                    self.reverse_index()
                elif data[index + 1] == ord("7"):
                    self.saved_cursor = (self.row, self.column)
                elif data[index + 1] == ord("8"):
                    self.row, self.column = self.saved_cursor
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
        return ["".join(cell[0] for cell in cells).rstrip()
                for cells in self.rows]

    def get_prompt_row(self):
        for row in range(len(self.rows) - 1, -1, -1):
            if any(cell[0] == "\u2022" for cell in self.rows[row]):
                return row
        return -1

    def get_prompt_width(self):
        row = self.get_prompt_row()
        if row < 0:
            return 0
        cells = self.rows[row]
        return next(index for index, cell in enumerate(cells)
                    if cell[0] == "•") + 2

    def get_typed_and_ghost(self):
        row = self.get_prompt_row()
        if row < 0:
            return None
        cells = self.rows[row]
        mark = self.get_prompt_width()
        typed = "".join(cell[0] for cell in cells[mark:] if not cell[1])
        ghost = "".join(cell[0] for cell in cells[mark:] if cell[1])
        return typed.rstrip(), ghost.rstrip()

    def get_menu(self):
        row = self.get_prompt_row()
        if row < 0:
            return None
        lines = self.get_lines()[row + 1:]
        if not lines:
            return None
        header = next((title for title in (MENU_HEADER, FUZZY_PATH_HEADER)
                       if lines[0].strip().startswith(title)), None)
        if header is None:
            return None
        expected_help = MENU_HELP.replace(MENU_HEADER, header, 1)
        help_text = lines[0].strip()
        first_entry = 1
        while (first_entry < len(lines) and help_text != expected_help
               and expected_help.startswith(
                   help_text + " " + lines[first_entry].strip())):
            help_text += " " + lines[first_entry].strip()
            first_entry += 1
        entries = []
        total = None
        entry_column = None
        for offset, line in enumerate(lines[first_entry:], row + 1 + first_entry):
            cells = self.rows[offset]
            text = line.strip()
            start = len(line) - len(line.lstrip())
            if text and not text.startswith(MENU_FOOTER):
                if entry_column is None:
                    entry_column = start
                elif start > entry_column:
                    continue
            detail = re.search(r" {2,}\S", line[start:])
            if detail and (cells[start + detail.end() - 1][1]
                           or text.startswith(("preview/", "dense/"))):
                text = line[start:start + detail.start()].rstrip()
            if text.startswith(MENU_FOOTER):
                match = re.match(r"showing \d+-\d+ of (\d+)(?= |$)", text)
                if match:
                    total = int(match[1])
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
                 rows=ROWS, rcfile="/dev/null", extra_environment=None):
        environment = {
            "PATH": command_directory,
            "HOME": directory,
            "KOSH_HISTORY_FILE": os.path.join(directory, "history"),
            "TERM": "xterm-256color",
            "LANG": "C.UTF-8",
            "RPS1": "",
        }
        environment.update(extra_environment or {})
        self.screen = Screen(rows)
        self.raw = bytearray()
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(directory)
            os.execve(binary, [binary, "-i", "--rcfile", rcfile], environment)
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

    def wait_for_raw_mode(self):
        deadline = time.monotonic() + WAIT_SECONDS
        while time.monotonic() < deadline:
            if not termios.tcgetattr(self.fd)[3] & termios.ICANON:
                return True
            if not self.pump(0.02):
                return False
        return False

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


def did_exit_cleanly(session):
    deadline = time.monotonic() + WAIT_SECONDS
    while time.monotonic() < deadline and session.pump(0.02):
        pass
    while time.monotonic() < deadline:
        finished, status = os.waitpid(session.pid, os.WNOHANG)
        if finished:
            session.is_closed = True
            os.close(session.fd)
            return (status == 0
                    and not re.search(
                        rb"(?:ERROR|SUMMARY): .*Sanitizer|runtime error:",
                        session.raw))
        time.sleep(0.02)
    return False


def get_state(screen):
    return screen.get_typed_and_ghost()


def is_line(typed, ghost=""):
    return lambda screen: get_state(screen) == (typed, ghost)


def has_caret_at(screen, offset):
    return (screen.row == screen.get_prompt_row()
            and screen.column == screen.get_prompt_width() + offset)


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


def is_menu_in_order(names):
    def do_check(screen):
        menu = screen.get_menu()
        return menu is not None and menu[0] == names
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


def has_search_row(screen, is_wanted):
    row = screen.get_prompt_row()
    lines = screen.get_lines()
    if row < 0 or row + 1 >= len(lines) or HISTORY_HEADER not in lines[row + 1]:
        return False
    return any(is_wanted(line.strip()) for line in lines[row + 2:])


def is_menu_closed(screen):
    return screen.get_menu() is None and get_state(screen) is not None


def has_hint(text):
    return lambda screen: text in screen.get_hint()


def has_hint_header(header):
    return lambda screen: screen.get_hint_header() == header


def is_hint_started_by(header, text):
    return lambda screen: (screen.get_hint_header() == header
                           and screen.get_hint().startswith(text))


def are_hint_rows_indented(screen):
    rows = screen.get_hint_rows()
    return len(rows) > 1 and all(line.startswith("  ") for line in rows)


def get_explain_words(screen):
    rows = screen.get_hint_rows()
    if not rows or rows[0].strip() != "synopsis":
        return None
    words = []
    for row in rows[1:]:
        text = row[2:]
        marker = "> " if text.startswith("> ") else ""
        fields = text[2:].split()
        words.append(marker + (fields[0] if fields else ""))
    return words


def is_explained(words):
    return lambda screen: get_explain_words(screen) == words


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
    # A Ctrl-C that only closes an open menu leaves the line, so the empty
    # line must last a moment before the next keys go out.
    for _ in range(3):
        session.send(CTRL_C)
        session.wait_until(is_line(""))
        settle(session, 0.05)
        if is_line("")(session.screen):
            return

def type_text(session, text):
    for byte in text:
        session.send(bytes([byte]))
        session.pump(0.02)


def is_diagnostic_row(screen):
    return (screen.get_hint() != ""
            and screen.get_hint_header() not in ("", "synopsis"))


def record_diagnostic(report, session, name, typed):
    type_text(session, typed)
    report.record(name, session, is_diagnostic_row)
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
                  lambda screen: is_explained(["act"])(screen)
                  and "act [command] [flags]" in screen.get_hint())
    session.send(b"-f")
    report.record("idle-hint-shows-flag-value", session,
                  lambda screen: is_explained(["act", "> -f"])(screen)
                  and "read the workflow from FILE" in screen.get_hint())
    clear_line(session)

    session.send(b"act run ")
    report.record("idle-hint-loads-subcommand-usage", session,
                  lambda screen: is_explained(["act"])(screen)
                  and "act run [--job NAME]" in screen.get_hint())
    clear_line(session)

    session.send(b"act ")
    session.wait_until(has_hint("act [command] [flags]"))
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

    with open(os.path.join(directory, "zzdefs.sh"), "w") as handle:
        handle.write("true\nzzfile() { echo FILE-RAN; }\n")
    session.send(b"source ./zzdefs.sh\r")
    session.wait_until(is_line(""))
    session.send(b"zzfile ")
    report.record("hint-shows-function-file", session,
                  lambda screen: is_explained(["zzfile"])(screen)
                  and "./zzdefs.sh:2" in screen.get_hint())
    clear_line(session)

    session.send(b"echo $(cat -n)")
    session.wait_until(is_line("echo $(cat -n)"))
    session.send(LEFT)
    report.record("hint-follows-command-substitution", session,
                  is_explained(["cat", "> -n"]))
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

    session.send(b"complete -C %s zzcase\r"
                 % os.path.join(directory, "case-words").encode())
    session.wait_until(is_line(""))
    session.send(b"zzcase a\t")
    report.record("command-spec-unranked-menu-opens", session,
                  has_typed_menu("zzcase a", ["apple", "Apricot", "avocado"]))
    session.send(b"p")
    report.record("command-spec-unranked-menu-keeps-every-prefix-row",
                  session,
                  lambda screen: has_typed_menu("zzcase ap",
                                                ["apple", "Apricot"])(screen)
                  and count_marker_lines(directory, "case-runs") == 1)
    clear_line(session)

    session.send(b"complete -E -W zzempty; complete -I -W zzinitial\r")
    session.wait_until(is_line(""))
    session.send(b"ls" + CTRL_A + b"\t")
    report.record("empty-slot-needs-an-empty-line", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0].rstrip() == "zzinitialls")
    clear_line(session)
    session.send(b"\t")
    report.record("empty-slot-serves-an-empty-line", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0].startswith("zzempty"))
    clear_line(session)
    session.send(b"complete -I -W zzslot\r")
    session.wait_until(is_line(""))
    session.send(b"zzs\t")
    report.record("initial-slot-completes-the-word-at-its-end", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0].rstrip() == "zzslot")
    clear_line(session)
    session.send(b"complete -r -E; complete -r -I\r")
    session.wait_until(is_line(""))
    session.send(b"complete -W zzarg zzone\r")
    session.wait_until(is_line(""))
    session.send(b"zzone zza" + LEFT * 3 + b"\t")
    report.record("argument-at-word-start-matches-and-replaces-the-word",
                  session, lambda screen: get_state(screen) is not None
                  and get_state(screen)[0].rstrip() == "zzone zzarg")
    clear_line(session)
    session.send(b"zzone zza" + LEFT * 2 + b"\t")
    report.record("argument-inside-word-matches-and-replaces-the-word", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0].rstrip() == "zzone zzarg")
    clear_line(session)
    session.send(b"complete -E -C %s\r"
                 % os.path.join(directory, "empty-words").encode())
    session.wait_until(is_line(""))
    session.send(b"\t")
    report.record("empty-slot-prefix-keeps-its-list", session,
                  lambda screen: has_typed_menu(
                      "zzempty", ["zzemptyA", "zzemptyB"])(screen)
                  and count_marker_lines(directory, "empty-runs") == 1)
    session.send(ESCAPE)
    session.wait_until(is_menu_closed)
    clear_line(session)
    session.send(b"complete -r -E\r")
    session.wait_until(is_line(""))


def run_compopt_checks(session, report):
    session.send(
        b"koshconf set completion.add_space_after_completed_word on; "
        b"koshconf set completion.menu_style plain\r")
    session.wait_until(is_line(""))
    session.send(b"_zzw() { [ \"$COMP_CWORD\" = 1 ] && COMPREPLY=(zzword); }; "
                 b"complete -F _zzw zzspaced; "
                 b"complete -o nospace -F _zzw zztight\r")
    session.wait_until(is_line(""))
    session.send(b"zzspaced zz\t")
    session.wait_until(lambda screen: get_state(screen) is not None
                       and get_state(screen)[0].startswith("zzspaced zzword"))
    session.send(b"Q")
    report.record("spec-completion-takes-a-space", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0] == "zzspaced zzword Q")
    clear_line(session)
    session.send(b"zztight zz\t")
    session.wait_until(lambda screen: get_state(screen) is not None
                       and get_state(screen)[0].startswith("zztight zzword"))
    session.send(b"Q")
    report.record("nospace-spec-keeps-the-caret-on-the-word", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0] == "zztight zzwordQ")
    clear_line(session)
    session.send(b"koshconf set completion.menu_style interactive\r")
    session.wait_until(is_line(""))
    session.send(b"_zzo() { COMPREPLY=(zeta alpha mid); }; "
                 b"complete -o nosort -F _zzo zzorder\r")
    session.wait_until(is_line(""))
    session.send(b"zzorder \t")
    report.record("nosort-menu-keeps-the-reply-order", session,
                  is_menu_in_order(["zeta", "alpha", "mid"]))
    clear_line(session)
    session.send(b"complete -W zzpath/ zzslash; "
                 b"koshconf set completion.menu_style plain\r")
    session.wait_until(is_line(""))
    for mode, line, name in (
            (b"on", "zzslash zzpath/ Q",
             "space-after-completion-on-follows-a-slash"),
            (b"on-excluding-trailing-slash", "zzslash zzpath/Q",
             "space-after-completion-skips-a-trailing-slash")):
        session.send(b"koshconf set completion.add_space_after_completed_word "
                     + mode + b"\r")
        session.wait_until(is_line(""))
        session.send(b"zzslash zz\t")
        session.wait_until(lambda screen: get_state(screen) is not None
                           and get_state(screen)[0].startswith(
                               "zzslash zzpath/"))
        session.send(b"Q")
        report.record(name, session,
                      lambda screen, line=line: get_state(screen) is not None
                      and get_state(screen)[0] == line)
        clear_line(session)
    session.send(b"zzspaced zz\t")
    session.wait_until(lambda screen: get_state(screen) is not None
                       and get_state(screen)[0].startswith("zzspaced zzword"))
    session.send(b"Q")
    report.record("space-after-completion-still-follows-a-word", session,
                  lambda screen: get_state(screen) is not None
                  and get_state(screen)[0] == "zzspaced zzword Q")
    clear_line(session)
    session.send(
        b"koshconf set completion.add_space_after_completed_word off; "
        b"koshconf set completion.menu_style interactive\r")
    session.wait_until(is_line(""))


def is_typed_without_menu(typed):
    return lambda screen: (is_menu_closed(screen)
                           and get_state(screen)[0] == typed)


def is_typed_with_menu(typed):
    return lambda screen: (screen.get_menu() is not None
                           and get_state(screen)[0] == typed)


def run_sole_completion_checks(session, report):
    for name, keys, typed, has_menu in (
            ("sole-directory-keeps-the-menu-inside", b"ls zzloc\tQ",
             "ls zzlocal/Q", True),
            ("second-tab-accepts-and-keeps-the-menu", b"ls zzloc\t\t",
             "ls zzlocal/inner.txt", True),
            ("sole-empty-directory-keeps-the-menu", b"ls zzhol\t\tQ",
             "ls zzhollow/Q", True),
            ("completion-undoes-in-one-step", b"cat sub/al\t" + CTRL_Z + b"Q",
             "cat sub/alQ", False),
            ("ctrl-c-closes-the-menu-and-clears", b"ls zzloc\t" + CTRL_C,
             "", False)):
        session.send(keys)
        report.record(name, session, is_typed_with_menu(typed) if has_menu
                      else is_typed_without_menu(typed))
        if has_menu:
            session.send(ESCAPE)
            session.wait_until(is_menu_closed)
        clear_line(session)


def run_path_spacing_checks(session, report):
    session.send(
        b"koshconf set completion.add_space_after_completed_word on\r")
    session.wait_until(is_line(""))
    for name, keys, completed in (
            ("fresh-tab-completes-a-file-without-space", b"cat sub/al\t",
             "cat sub/alpha-beta.txt"),
            ("fresh-tab-completes-a-directory-without-space", b"ls zzloc\t",
             "ls zzlocal/")):
        session.send(keys)
        report.record(name, session,
                      lambda screen: is_typed_with_menu(completed)(screen)
                      and has_caret_at(screen, len(completed)))
        session.send(ESCAPE)
        session.wait_until(is_menu_closed)
        session.send(b"Q")
        report.record(name + "-and-escape-closes-it", session,
                      is_typed_without_menu(completed + "Q"))
        clear_line(session)
    session.send(b"cat menu/menu-\t")
    opened = session.wait_until(is_typed_with_menu("cat menu/menu-"))
    session.send(b"\t")
    completed = "cat menu/menu-apple"
    report.record("menu-tab-still-appends-a-space-to-a-file", session,
                  lambda screen: opened and is_typed_with_menu(completed)(screen)
                  and has_caret_at(screen, len(completed) + 1))
    session.send(ESCAPE)
    session.wait_until(is_menu_closed)
    session.send(CTRL_Z + b"Q")
    report.record("menu-completion-and-space-undo-in-one-step", session,
                  is_typed_without_menu("cat menu/menu-Q"))
    clear_line(session)
    session.send(
        b"koshconf set completion.add_space_after_completed_word off\r")
    session.wait_until(is_line(""))


def settle(session, seconds):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        session.pump(0.02)


def run_next_word_menu_checks(session, report, directory):
    session.send(b"complete -C %s zznw; complete -C %s zznone\r"
                 % (os.path.join(directory, "next-words").encode(),
                    os.path.join(directory, "next-words").encode()))
    session.wait_until(is_line(""))
    session.send(
        b"koshconf set completion.add_space_after_completed_word on\r")
    session.wait_until(is_line(""))
    before = count_marker_lines(directory, "next-runs")
    session.send(b"zznw zznwf\t")
    report.record("next-word-menu-opens-after-the-space", session,
                  lambda screen: has_typed_menu(
                      "zznw zznwfirst", ["zzalpha", "zzbeta"])(screen))
    report.record("next-word-menu-shows-no-duplicate-preview", session,
                  lambda screen: get_state(screen) == ("zznw zznwfirst", ""))
    report.record("next-word-menu-runs-the-callback-once-per-word", session,
                  lambda screen: count_marker_lines(
                      directory, "next-runs") - before == 2)
    session.send(b"zzb")
    report.record("next-word-menu-narrows-by-typing", session,
                  lambda screen: has_typed_menu(
                      "zznw zznwfirst zzb", ["zzbeta"])(screen))
    session.send(ESCAPE)
    report.record("next-word-menu-closes-on-escape", session,
                  is_typed_without_menu("zznw zznwfirst zzb"))
    clear_line(session)
    session.send(b"zznw zznwf\t")
    session.wait_until(lambda screen: screen.get_menu() is not None)
    session.send(b"zz")
    session.wait_until(lambda screen: get_state(screen)[0].endswith("zz"))
    session.send(b"\x07")
    session.wait_until(is_menu_closed)
    session.send(b"Q")
    report.record("next-word-menu-restores-on-ctrl-g", session,
                  is_typed_without_menu("zznw zznwfirst Q"))
    clear_line(session)
    session.send(b"zznw zznwf\t")
    session.wait_until(lambda screen: screen.get_menu() is not None)
    session.send(b"\r")
    report.record("next-word-menu-enter-runs-the-line", session,
                  lambda screen: get_state(screen) == ("", "")
                  and any("was not found" in line
                          for line in screen.get_lines()))
    clear_line(session)
    session.send(b"_zzn() { echo run >> %s; "
                 b"[ \"$COMP_CWORD\" = 1 ] && COMPREPLY=(zznonefirst); :; }; "
                 b"complete -o bashdefault -F _zzn zznone\r"
                 % os.path.join(directory, "next-runs").encode())
    session.wait_until(is_line(""))
    before = count_marker_lines(directory, "next-runs")
    session.send(b"zznone zznonef\t")
    session.wait_until(lambda screen: count_marker_lines(
        directory, "next-runs") - before == 2)
    settle(session, 0.5)
    report.record("next-word-without-candidates-keeps-the-menu", session,
                  is_typed_with_menu("zznone zznonefirst"))
    session.send(b"Q")
    report.record("next-word-without-candidates-keeps-typing", session,
                  is_typed_with_menu("zznone zznonefirst Q"))
    session.send(ESCAPE)
    session.wait_until(is_menu_closed)
    clear_line(session)
    session.send(
        b"koshconf set completion.add_space_after_completed_word off\r")
    session.wait_until(is_line(""))
    before = count_marker_lines(directory, "next-runs")
    session.send(b"zznw zznwf\t")
    session.wait_until(lambda screen: count_marker_lines(
        directory, "next-runs") - before >= 1)
    settle(session, 0.5)
    report.record("sole-word-keeps-the-menu-with-the-option-off", session,
                  is_typed_with_menu("zznw zznwfirst"))
    session.send(ESCAPE)
    session.wait_until(is_menu_closed)
    clear_line(session)


def record_default_completion_space(session, report):
    session.send(b"complete -W zzword zzdefault\r")
    session.wait_until(is_line(""))
    session.send(b"zzdefault z\t")
    completed = "zzdefault zzword"
    report.record("tab-appends-a-space-by-default", session,
                  lambda screen: is_typed_with_menu(completed)(screen)
                  and has_caret_at(screen, len(completed) + 1))
    clear_line(session)
    session.send(b"complete -r zzdefault\r")
    session.wait_until(is_line(""))


def run_completion_key_checks(session, report, directory):
    session.send(
        b"koshconf set completion.add_space_after_completed_word off; "
        b"complete -W 'alpha beta' zzkey; "
        b"complete -o nospace -W 'alpha beta' zzkeytight\r")
    session.wait_until(is_line(""))

    def at_caret(typed, offset):
        return lambda screen: (get_state(screen) is not None
                               and get_state(screen)[0] == typed
                               and has_caret_at(screen, offset))

    def has_choices(typed):
        return has_typed_menu(typed, ["alpha", "beta"])

    def accept_beta(command):
        session.send(command.encode() + b" \t")
        if not session.wait_until(has_choices(command)):
            return False
        session.send(DOWN + CTRL_RIGHT)
        return True

    with_space = at_caret("zzkey beta", len("zzkey beta "))
    accepted = accept_beta("zzkey")
    report.record("ctrl-right-accepts-bash-row-with-space", session,
                  lambda screen: accepted and has_choices("zzkey beta")(screen)
                  and with_space(screen))
    session.send(CTRL_RIGHT)
    report.record("ctrl-right-accepts-a-second-bash-word", session,
                  at_caret("zzkey beta alpha", len("zzkey beta alpha ")))
    session.send(CTRL_LEFT)
    report.record("ctrl-left-erases-bash-word-and-space", session,
                  lambda screen: has_choices("zzkey beta")(screen)
                  and with_space(screen))
    session.send(CTRL_LEFT)
    report.record("ctrl-left-erases-the-earlier-bash-word-and-space", session,
                  lambda screen: has_choices("zzkey")(screen)
                  and at_caret("zzkey", len("zzkey "))(screen))
    clear_line(session)

    session.send(b"zzkey beta \t")
    opened = session.wait_until(has_choices("zzkey beta"))
    session.send(CTRL_LEFT)
    report.record("ctrl-left-only-moves-over-a-typed-word", session,
                  lambda screen: opened and screen.get_menu() is not None
                  and at_caret("zzkey beta", len("zzkey "))(screen))
    clear_line(session)

    accepted = accept_beta("zzkey") and session.wait_until(with_space)
    session.send(BACKSPACE)
    report.record("backspace-only-erases-the-bash-completion-space", session,
                  lambda screen: accepted and at_caret(
                      "zzkey beta", len("zzkey beta"))(screen))
    clear_line(session)

    accepted = accept_beta("zzkey") and session.wait_until(with_space)
    session.send(LEFT * len("beta ") + DELETE)
    report.record("delete-only-erases-one-bash-completion-character", session,
                  lambda screen: accepted and at_caret(
                      "zzkey eta", len("zzkey "))(screen))
    clear_line(session)

    accepted = accept_beta("zzkeytight")
    report.record("ctrl-right-honors-bash-nospace", session,
                  lambda screen: accepted and at_caret(
                      "zzkeytight beta", len("zzkeytight beta"))(screen))
    clear_line(session)

    session.send(b"complete -I -W zzslot\r")
    session.wait_until(is_line(""))
    session.send(b"zzslo tail" + LEFT * len("lo tail") + b"\t")
    report.record("initial-slot-replaces-the-whole-word", session,
                  at_caret("zzslot tail", len("zzslot")))
    clear_line(session)
    session.send(b"complete -r -I; complete -r zzkey zzkeytight\r")
    session.wait_until(is_line(""))

    session.send(b"koshconf set mood bash\r")
    session.wait_until(is_line(""))
    session.send(b"complete -I -W zzinsert\r")
    session.wait_until(is_line(""))
    session.send(b"ls" + HOME + b"\t")
    report.record("bash-initial-slot-fresh-tab-inserts-at-word-start", session,
                  at_caret("zzinsertls", len("zzinsert")))
    clear_line(session)
    session.send(b"complete -I -W 'zzinsertA zzinsertB'\r")
    session.wait_until(is_line(""))
    session.send(b"ls" + HOME + b"\t")
    opened = session.wait_until(
        lambda screen: has_typed_menu(
            "zzinsertls", ["zzinsertA", "zzinsertB"])(screen)
        and at_caret("zzinsertls", len("zzinsert"))(screen))
    session.send(DOWN + b"\t")
    report.record("bash-initial-slot-fresh-menu-replaces-insertion-and-word",
                  session, lambda screen: opened
                  and at_caret("zzinsertB", len("zzinsertB"))(screen))
    clear_line(session)
    session.send(b"complete -r -I\r")
    session.wait_until(is_line(""))
    session.send(b"zzswapA() { :; }; zzswapMedium() { :; }; "
                 b"zzswapLongestName() { :; }; zzswing() { :; }\r")
    session.wait_until(is_line(""))
    tail = " --flag 'two words'"
    stem = "zzswap"
    choices = ["zzswapA", "zzswapLongestName", "zzswapMedium"]
    draft = stem + tail
    offset = len("zzsw")
    session.send(draft.encode() + LEFT * (len(draft) - offset) + b"\t")
    report.record("command-tab-queries-the-whole-token-without-growing-it",
                  session, lambda screen: has_typed_menu(draft, choices)(screen)
                  and at_caret(draft, offset)(screen))
    for attempt, index in enumerate((2, 0, 1), 1):
        ready = session.wait_until(
            lambda screen: is_menu_in_order(choices)(screen)
            and at_caret(draft, offset)(screen))
        session.send(DOWN * index + b"\t")
        candidate = choices[index]
        report.record("bash-command-reselection-replaces-word-%d" % attempt,
                      session, lambda screen: ready
                      and is_typed_with_menu(candidate + tail)(screen)
                      and at_caret(candidate + tail, len(candidate))(screen))
        if attempt < len(choices):
            session.send(BACKSPACE * (len(candidate) - len(stem))
                         + LEFT * (len(stem) - offset))
    clear_line(session)

    session.send(b"complete -I -W 'zzslotA zzslotLong zzslotMedium zzslip'\r")
    session.wait_until(is_line(""))
    stem = "zzslot"
    draft = stem + tail
    choices = ["zzslotA", "zzslotLong", "zzslotMedium"]
    session.send(draft.encode() + LEFT * (len(draft) - 4) + b"\t" + HOME)
    for attempt, (index, accept) in enumerate(((1, b"\t"), (2, CTRL_RIGHT)), 1):
        ready = session.wait_until(
            lambda screen: is_menu_in_order(choices)(screen)
            and at_caret(draft, 0)(screen))
        session.send(DOWN * index + accept)
        candidate = choices[index]
        caret = len(candidate) + (accept == CTRL_RIGHT)
        report.record("bash-initial-slot-reselection-replaces-word-%d" % attempt,
                      session, lambda screen: ready
                      and is_typed_with_menu(candidate + tail)(screen)
                      and at_caret(candidate + tail, caret)(screen))
        if accept == b"\t":
            session.send(BACKSPACE * (len(candidate) - len(stem)) + HOME)
    clear_line(session)

    session.send(b"complete -r -I; "
                 b"unset -f zzswapA zzswapMedium zzswapLongestName zzswing; "
                 b"koshconf set mood kosh\r")
    session.wait_until(is_line(""))

    session.send(b": > zz-glob-a.txt; : > zz-glob-b.txt; : > zz-glob-c.log\r")
    session.wait_until(is_line(""))
    original = b"cat zz-glob-*.txt"
    expanded = "cat zz-glob-a.txt zz-glob-b.txt"
    for position, motion in (("end", b""), ("middle", LEFT * len(".txt"))):
        session.send(original + motion + b"\t")
        report.record("explicit-glob-expands-from-token-" + position, session,
                      at_caret(expanded, len(expanded)))
        clear_line(session)

    session.send(b"koshconf set completion.add_space_after_completed_word on; "
                 b"zzaccept() { printf 'ACCEPTED:%s\\n' \"$*\"; }; "
                 b"complete -W 'alpha beta' zzaccept\r")
    session.wait_until(is_line(""))
    session.send(b"zzaccept \t")
    opened = session.wait_until(has_typed_menu("zzaccept", ["alpha", "beta"]))
    prompt_row = session.screen.get_prompt_row()
    prompt = session.screen.get_lines()[prompt_row][
        :session.screen.get_prompt_width()]
    mark = len(session.raw)

    def is_same_draft(screen, typed, caret):
        return (screen.get_prompt_row() == prompt_row
                and screen.get_lines()[prompt_row].startswith(prompt)
                and get_state(screen)[0] == typed
                and has_caret_at(screen, caret)
                and b"\x1b]133;A" not in session.raw[mark:]
                and b"\x1b]133;C" not in session.raw[mark:])

    session.send(DOWN + CTRL_ENTER_KITTY)
    report.record("kitty-ctrl-enter-keeps-the-prompt-and-next-menu", session,
                  lambda screen: opened
                  and has_typed_menu("zzaccept beta", ["alpha", "beta"])(screen)
                  and is_same_draft(screen, "zzaccept beta", len("zzaccept beta ")))
    session.send(CTRL_ENTER_XTERM)
    report.record("xterm-ctrl-enter-accepts-the-next-word-without-running", session,
                  lambda screen: is_typed_with_menu("zzaccept beta alpha")(screen)
                  and is_same_draft(screen, "zzaccept beta alpha",
                                    len("zzaccept beta alpha ")))
    session.send(b"kept")
    report.record("ctrl-enter-leaves-the-draft-editable", session,
                  lambda screen: has_typed_menu("zzaccept beta alpha kept",
                                                [HISTORY_NO_MATCH])(screen)
                  and is_same_draft(screen, "zzaccept beta alpha kept",
                                    len("zzaccept beta alpha kept")))
    run_command(session, report, "plain-enter-submits-the-edited-draft", b"",
                "ACCEPTED:beta alpha kept", 1)
    session.send(b"complete -r zzaccept; unset -f zzaccept\r")
    session.wait_until(is_line(""))

    run_sibling_completion_checks(session, report, directory)
    session.send(
        b"koshconf set completion.add_space_after_completed_word off\r")
    session.wait_until(is_line(""))

    run_fuzzy_path_checks(session, report, directory)
    run_streaming_fuzzy_checks(session, report, directory)


def run_sibling_completion_checks(session, report, directory):
    parent = "./siblings/"
    os.mkdir(os.path.join(directory, parent))
    for name in ("Aa.cpp", "Bc.cpp", "Bd.cpp"):
        with open(os.path.join(directory, parent, name), "w") as handle:
            handle.write("SIBLING-COMMAND-RAN\n")

    initial = "cat " + parent + "B"
    session.send(initial.encode() + b"\t")
    opened = session.wait_until(is_typed_with_menu(initial))
    screen = session.screen
    row = screen.get_prompt_row()
    prompt = screen.get_lines()[row][:screen.get_prompt_width()]
    mark = len(session.raw)
    draft = "cat "

    def has_highlighted_sibling(name):
        typed = draft + parent

        def do_check(screen):
            return (opened and screen.get_menu() is not None
                    and screen.get_prompt_row() == row
                    and screen.get_lines()[row].startswith(prompt)
                    and get_state(screen)[0] == typed
                    and has_caret_at(screen, len(typed))
                    and any(name in line and "\xa0" in line
                            for line in screen.get_lines()[row + 1:])
                    and b"\x1b]133;A" not in session.raw[mark:]
                    and b"\x1b]133;C" not in session.raw[mark:]
                    and b"SIBLING-COMMAND-RAN" not in session.raw[mark:])
        return do_check

    session.send(CTRL_ENTER_KITTY)
    draft += parent + "Bc.cpp "
    report.record("ctrl-enter-seeds-the-parent-and-highlights-the-accepted-file",
                  session, has_highlighted_sibling("Bc.cpp"))
    session.send(DOWN + CTRL_ENTER_XTERM)
    draft += parent + "Bd.cpp "
    report.record("ctrl-enter-repeats-with-a-different-sibling-without-running",
                  session, has_highlighted_sibling("Bd.cpp"))
    clear_line(session)

    parent = "./nav/"
    os.mkdir(os.path.join(directory, parent))
    for child in ("aa", "sub"):
        os.mkdir(os.path.join(directory, parent, child))
        for entry in ("alpha", "beta"):
            open(os.path.join(directory, parent, child, entry), "w").close()
    typed = "cat " + parent
    escaped = typed + r"s\ub/"
    session.send(escaped.encode() + b"\t")
    descended = session.wait_until(is_typed_with_menu(escaped))
    session.send(CTRL_LEFT)
    report.record("ctrl-left-highlights-the-directory-it-just-exited", session,
                  lambda screen: descended
                  and is_typed_with_menu(typed)(screen)
                  and any(parent + "sub/" in line and "\xa0" in line for line in
                          screen.get_lines()[screen.get_prompt_row() + 1:]))
    clear_line(session)


def run_fuzzy_path_checks(session, report, directory):
    folder = "fz/i"
    os.makedirs(os.path.join(directory, folder, "n"))
    for file in (folder + "/data-done.txt", folder + "/data-dusk.txt",
                 folder + "/n/data-draft.txt",
                 "fz/data-drop.txt", "data-draw.txt"):
        open(os.path.join(directory, file), "w").close()
    os.symlink("..", os.path.join(directory, folder, "escape"))
    os.symlink(".", os.path.join(directory, folder, "loop"))

    prefix = "cat ./" + folder + "/"
    root = prefix[len("cat "):]
    draft = prefix + "dadq"
    miss = [HISTORY_NO_MATCH]

    def has_fuzzy_menu(typed, names):
        def do_check(screen):
            menu = screen.get_menu()
            return (menu is not None
                    and get_state(screen)[0] == typed
                    and sorted(re.split(r"\s{2,}", row, maxsplit=1)[0]
                               .removeprefix(root) for row in menu[0])
                    == sorted(names)
                    and FUZZY_PATH_HEADER in
                    screen.get_lines()[screen.get_prompt_row() + 1])
        return do_check

    session.send(draft.encode() + b"\t")
    report.record("first-path-tab-advertises-fuzzy-search", session,
                  has_typed_menu(draft, ["no matches. tab to fuzzy search"]))
    mark = len(session.raw)
    session.send(b"\t")
    report.record("second-path-tab-seeds-fuzzy-search", session,
                  has_fuzzy_menu(draft, miss))
    session.send(BACKSPACE)
    report.record("fuzzy-path-backspace-finds-descendants-without-escaping",
                  session, has_fuzzy_menu(prefix + "dad",
                                         ["data-done.txt", "data-dusk.txt",
                                          "n/data-draft.txt"]))
    query = root + "dad"
    first = root + "data-done.txt"
    second = root + "data-dusk.txt"
    choices = ["data-done.txt", "data-dusk.txt", "n/data-draft.txt"]
    session.send(CTRL_ENTER_KITTY)
    chained = "cat " + first + " " + query
    report.record("fuzzy-ctrl-enter-pastes-and-retains-the-query", session,
                  lambda screen: has_fuzzy_menu(chained, choices)(screen)
                  and has_caret_at(screen, len(chained)))
    session.send(DOWN + CTRL_ENTER_XTERM)
    chained = "cat " + first + " " + second + " " + query
    report.record("fuzzy-ctrl-enter-pastes-a-second-result", session,
                  lambda screen: has_fuzzy_menu(chained, choices)(screen)
                  and has_caret_at(screen, len(chained)))
    prefix = "cat " + first + " " + second + " " + root
    session.send(b"r")
    report.record("fuzzy-path-typing-narrows-the-menu", session,
                  has_fuzzy_menu(prefix + "dadr", ["n/data-draft.txt"]))
    session.send(b"\t")
    accepted = prefix + "n/data-draft.txt"
    report.record("fuzzy-path-tab-accepts-the-scoped-match", session,
                  lambda screen: is_line(accepted)(screen)
                  and has_caret_at(screen, len(accepted)))
    session.send(ESCAPE)
    report.record("fuzzy-path-escape-keeps-the-draft-without-running", session,
                  lambda screen: is_typed_without_menu(accepted)(screen)
                  and b"\x1b]133;A" not in session.raw[mark:]
                  and b"\x1b]133;C" not in session.raw[mark:])
    clear_line(session)

    draft = "cat " + root + "dadq"
    session.send(draft.encode() + b"\t")
    session.wait_until(has_typed_menu(draft, ["no matches. tab to fuzzy search"]))
    session.send(b"\t")
    session.wait_until(has_fuzzy_menu(draft, miss))
    session.send(BACKSPACE * len("dadq") + b"'dadr")
    report.record("fuzzy-path-quote-at-root-refreshes-the-query", session,
                  has_fuzzy_menu("cat " + root + "'dadr", ["'n/data-draft.txt'"]))
    quoted = root + "'dadr"
    session.send(CTRL_ENTER_KITTY)

    def has_retained_quoted_query(screen):
        state = get_state(screen)
        if state is None or not state[0].endswith(" " + quoted):
            return False
        typed = state[0]
        try:
            words = shlex.split(typed[:-(len(quoted) + 1)])
        except ValueError:
            return False
        return (words == ["cat", root + "n/data-draft.txt"]
                and has_caret_at(screen, len(typed))
                and screen.get_menu() is not None
                and FUZZY_PATH_HEADER in
                screen.get_lines()[screen.get_prompt_row() + 1])

    report.record("fuzzy-ctrl-enter-closes-only-the-accepted-quote", session,
                  has_retained_quoted_query)
    clear_line(session)


def run_streaming_fuzzy_checks(session, report, directory):
    tree = os.path.join(directory, "tree")
    branch = os.path.join(tree, "wide")
    os.makedirs(branch)
    for index in range(8192):
        name = ("needle-%04d.txt" if index % 96 == 0
                else "ordinary-%04d.txt") % index
        open(os.path.join(branch, name), "w").close()
    open(os.path.join(branch, "needle-special.txt"), "w").close()
    open(os.path.join(directory, "needle-special.txt"), "w").close()
    os.symlink("..", os.path.join(tree, "outside"))

    draft = "cat ./tree/ndle"

    def fuzzy_rows(screen):
        rows = screen.get_lines()[screen.get_prompt_row() + 1:]
        if not rows or FUZZY_PATH_HEADER not in rows[0]:
            return []
        return [line.strip() for line in rows if line.strip()]

    def is_loading(screen):
        return fuzzy_rows(screen)[-1:] == ["loading..."]

    def start_search():
        session.send(draft.encode() + b"\t")
        session.wait_until(has_typed_menu(draft, ["no matches. tab to fuzzy search"]))
        session.send(b"\t")

    start_search()
    report.record("fuzzy-path-streams-matches-above-the-loading-footer", session,
                  lambda screen: is_loading(screen)
                  and any("needle-" in row for row in fuzzy_rows(screen)))
    session.send(b"s")
    report.record("fuzzy-path-stream-keeps-edits-and-finishes-in-scope", session,
                  lambda screen: get_state(screen)[0] == draft + "s"
                  and not is_loading(screen)
                  and [row.split()[0] for row in fuzzy_rows(screen)
                       if row.startswith("./")]
                  == ["./tree/wide/needle-special.txt"])
    clear_line(session)

    start_search()
    loading = session.wait_until(is_loading)
    mark = len(session.raw)
    session.send(ESCAPE)
    report.record("fuzzy-path-escape-stops-a-loading-search", session,
                  lambda screen: loading and is_typed_without_menu(draft)(screen)
                  and b"\x1b]133;C" not in session.raw[mark:])
    clear_line(session)

    start_search()
    def selected_path(screen):
        for row in screen.get_lines()[screen.get_prompt_row() + 1:]:
            found = re.search(r"\xa0(\./tree/wide/needle-[^\s]+\.txt)", row)
            if found:
                return found[1]
        return None

    ready = session.wait_until(lambda screen: not is_loading(screen)
                               and selected_path(screen) is not None)
    ordered = ["./tree/wide/needle-%04d.txt" % index
               for index in range(0, 8192, 96)]
    ordered.append("./tree/wide/needle-special.txt")
    selected = selected_path(session.screen)
    expected = (ordered[(ordered.index(selected) + 20) % len(ordered)]
                if selected in ordered else None)
    session.send(DOWN * 20)
    report.record("fuzzy-path-scrolls-in-final-order", session,
                  lambda screen: ready and expected is not None
                  and selected_path(screen) == expected)
    clear_line(session)


def run_checks(binary, directory, command_directory, report):
    session = Session(binary, directory, command_directory)
    try:
        if not report.record("startup-prompt", session, is_line("")):
            return
        record_default_completion_space(session, report)
        session.send(
            b"koshconf set completion.add_space_after_completed_word off\r")
        session.wait_until(is_line(""))

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
        clear_line(session)

        session.send(b"cat sub/al")
        session.wait_until(is_line("cat sub/al", "pha-beta.txt"))
        session.send(CTRL_E)
        report.record("ghost-ctrl-e-accepts", session,
                      is_line("cat sub/alpha-beta.txt"))
        clear_line(session)

        session.send(b"CDPATH=%s\r"
                     % os.path.join(directory, "cdpath").encode())
        session.wait_until(is_line(""))
        time.sleep(0.6)
        session.pump(0.05)
        type_text(session, b"cd cdpath-t")
        report.record("ghost-reads-cdpath-after-a-pause", session,
                      is_line("cd cdpath-t", "arget/"))
        clear_line(session)
        session.send(b"unset CDPATH\r")
        session.wait_until(is_line(""))

        late_file = os.path.join(directory, "late", "late-file")
        session.send(b"{ sleep 0.5; : > late/late-file; } &\r")
        session.wait_until(is_line(""))
        session.send(b"cat late/la\t")
        session.pump(0.1)
        deadline = time.monotonic() + WAIT_SECONDS
        while not os.path.exists(late_file) and time.monotonic() < deadline:
            session.pump(0.05)
        session.send(b"\t")
        report.record("tab-lists-file-created-after-prompt", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0].startswith("cat late/late-file"))
        clear_line(session)

        session.send(b"cat later/")
        session.pump(0.3)
        session.send(BACKSPACE * len("cat later/"))
        session.wait_until(is_line(""))
        open(os.path.join(directory, "later", "later-file"), "w").close()
        session.send(b"\r")
        session.wait_until(is_line(""))
        session.pump(0.1)
        session.send(b"cat later/la")
        report.record("empty-enter-refreshes-directory-listings", session,
                      is_line("cat later/la", "ter-file"))
        clear_line(session)

        session.send(b"cat menu/menu-")
        session.send(b"\t")
        names = ["menu/menu-apple", "menu/menu-apricot", "menu/menu-avocado",
                 "menu/menu-banana"]
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
        run_compopt_checks(session, report)
        run_sole_completion_checks(session, report)
        run_path_spacing_checks(session, report)
        run_next_word_menu_checks(session, report, directory)
        run_completion_key_checks(session, report, directory)

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

        session.send(b"@@@@" + CTRL_R)
        report.record("ctrl-r-says-no-match", session,
                      lambda screen: has_search_row(
                          screen, lambda row: row == HISTORY_NO_MATCH))
        session.send(ESCAPE)
        session.wait_until(lambda screen: not has_search_row(
            screen, lambda row: row == HISTORY_NO_MATCH))
        clear_line(session)
        long_command = b"echo hist-long-" + b"y" * 130
        session.send(long_command + b"\r")
        session.wait_until(lambda screen: any(
            line.startswith("hist-long-y") for line in screen.get_lines()))
        session.wait_until(is_line(""))
        session.wait_for_raw_mode()
        session.send(b"hist-long" + CTRL_R)
        report.record("ctrl-r-cuts-a-long-row-with-an-ellipsis", session,
                      lambda screen: has_search_row(
                          screen, lambda row: row.startswith("echo hist-long-y")
                          and row.endswith("yyy...")))
        session.send(ESCAPE)
        session.wait_until(lambda screen: not has_search_row(
            screen, lambda row: row.endswith("yyy...")))
        clear_line(session)

        session.send(b"koshconf set editor.history.arrow_keys_search_by_typed_prefix off\r")
        session.wait_until(is_line(""))
        session.send(b"echo hist-")
        session.wait_until(is_line("echo hist-", "gamma"))
        session.send(UP)
        report.record("option-off-up-recalls-newest-entry", session,
                      is_line("koshconf set editor.history.arrow_keys_search_by_typed_prefix off"))
        clear_line(session)

        session.send(b"cat ")
        report.record("hint-explains-the-command", session,
                      is_explained(["cat"]))
        report.record("hint-rows-indented", session, are_hint_rows_indented)
        session.send(b"-n")
        report.record("hint-explains-the-flag-under-the-caret", session,
                      lambda screen: is_explained(["cat", "> -n"])(screen)
                      and screen.get_hint_rows()[-1].strip() != "> -n")
        session.send(BACKSPACE * 3)
        report.record("hint-clears-inside-command-word", session,
                      is_without_hint("cat"))
        clear_line(session)

        session.send(b"ls -l -a -h -R -t" + LEFT * 7)
        report.record("hint-lists-more-than-three-flags", session,
                      is_explained(["ls", "-l", "-a", "> -h", "-R", "-t"]))
        clear_line(session)

        session.send(b"ls -l sub -a" + LEFT * 5)
        report.record("hint-lists-flags-around-an-operand", session,
                      is_explained(["ls", "-l", "-a"]))
        clear_line(session)

        session.send(b"ls -l $(echo sub) -a" + LEFT * 15)
        report.record("hint-lists-flags-past-a-substitution", session,
                      is_explained(["ls", "> -l", "-a"]))
        clear_line(session)

        session.send(b"echo `ls -l` -n" + LEFT * 4)
        report.record("hint-stops-at-the-closing-backtick", session,
                      is_explained(["ls", "> -l"]))
        clear_line(session)

        session.send(b"echo ")
        report.record("hint-explains-a-builtin", session,
                      is_explained(["echo"]))
        clear_line(session)

        session.send(b"koshconf set ")
        report.record("hint-skips-a-word-without-description", session,
                      is_explained(["koshconf"]))
        clear_line(session)

        session.send(b"koshconf get maxent\t")
        report.record("tab-completes-option-name-by-subsequence", session,
                      lambda screen: get_state(screen) is not None
                      and get_state(screen)[0].startswith(
                          "koshconf get editor.history.max_entries"))
        session.wait_until(lambda screen: screen.get_menu() is not None)
        session.send(ESCAPE)
        session.wait_until(is_menu_closed)
        clear_line(session)

        session.send(b"koshconf set \t")
        session.wait_until(lambda screen: screen.get_menu() is not None)
        settle(session, 0.5)
        session.send(b"q")
        settle(session, 0.5)
        session.send(b"uotes")
        report.record("menu-finds-a-subsequence-after-an-empty-byte", session,
                      is_menu(["editor.auto_close_brackets_and_quotes"]))
        session.send(ESCAPE)
        session.wait_until(is_menu_closed)
        clear_line(session)

        session.send(b"koshconf set \t")
        session.wait_until(lambda screen: screen.get_menu() is not None)
        settle(session, 0.5)
        session.send(b"a")
        settle(session, 0.5)
        session.send(b"uto_close")
        report.record("menu-keeps-matches-typed-after-a-first-letter", session,
                      lambda screen: screen.get_menu() is not None
                      and "editor.auto_close_brackets_and_quotes"
                      in screen.get_menu()[0])
        session.send(ESCAPE)
        session.wait_until(is_menu_closed)
        clear_line(session)

        session.send(b"zzprobe-one ")
        session.wait_until(is_line("zzprobe-one"))
        session.pump(0.3)
        report.record("hint-absent-for-uncached-command", session,
                      is_without_hint("zzprobe-one"))
        clear_line(session)

        session.send(b"nproc ")
        session.wait_until(is_line("nproc"))
        session.pump(0.4)
        report.record("hint-path-program-hides-bundled-synopsis", session,
                      is_without_hint("nproc"))
        clear_line(session)

        session.send(b"koshkit nproc ")
        report.record("hint-koshkit-explains-the-utility", session,
                      is_explained(["koshkit", "nproc"]))
        clear_line(session)

        session.send(b"cat menu/menu-")
        session.wait_until(is_explained(["cat"]))
        session.send(b"\t")
        report.record("hint-yields-to-menu", session, is_menu(names))
        session.send(ESCAPE)
        report.record("hint-returns-after-menu-closes", session,
                      lambda screen: is_menu_closed(screen)
                      and is_explained(["cat"])(screen))
        clear_line(session)

        session.send(b"cat -n sub/alpha-beta.txt")
        session.wait_until(lambda screen: get_explain_words(screen) is not None
                           and "-n" in get_explain_words(screen))
        session.send(b"\r")
        report.record("hint-erased-on-submit", session,
                      lambda screen: get_state(screen) == ("", "")
                      and any("ALPHA-CONTENT" in line
                              for line in screen.get_lines()[:-1])
                      and not any(line.strip() == "synopsis"
                                  for line in screen.get_lines()))

        run_idle_hint_checks(session, report, directory)

        record_diagnostic(report, session, "diagnostic-double-quote",
                          b'echo "abc')
        type_text(session, b"echo hi; fi")
        report.record("diagnostic-shown-at-word-end", session,
                      is_diagnostic_row)
        session.send(b" ")
        report.record("diagnostic-shown-after-word", session,
                      is_diagnostic_row)
        clear_line(session)
        type_text(session, b'echo "abc')
        session.wait_until(is_diagnostic_row)
        session.send(b'"')
        report.record("diagnostic-clears-when-quote-closes", session,
                      is_explained(["echo"]))
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
        session.send(b"koshconf set editor.auto_close_brackets_and_quotes on\r")
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
        type_text(session, b" (\" [")
        report.record("auto-pair-single-quoted-text-stays-plain", session,
                      is_line("echo (a) don' (\" ["))
        clear_line(session)
        type_text(session, b'echo "a (')
        report.record("auto-pair-double-quoted-text-stays-plain", session,
                      is_line('echo "a ("'))
        clear_line(session)
        type_text(session, b'echo "$(')
        report.record("auto-pair-substitution-in-paired-quote", session,
                      is_line('echo "$()"'))
        clear_line(session)
        type_text(session, b'echo "${')
        report.record("auto-pair-expansion-in-paired-quote", session,
                      is_line('echo "${}"'))
        clear_line(session)
        type_text(session, b"(case x in a)")
        report.record("auto-pair-case-pattern-keeps-subshell-closer", session,
                      is_line("(case x in a))"))
        clear_line(session)
        type_text(session, b"echo $(case x in (a)")
        report.record("auto-pair-parenthesized-pattern-steps-over", session,
                      is_line("echo $(case x in (a))"))
        clear_line(session)
        session.send(b"koshconf set editor.auto_close_brackets_and_quotes off\r")
        session.wait_until(is_line(""))

        session.send(b"koshconf set editor.hints.show_command_synopsis off\r")
        session.wait_until(is_line(""))
        session.send(b"cat ")
        session.wait_until(is_line("cat"))
        session.pump(0.3)
        report.record("option-off-hides-hint", session, is_without_hint("cat"))
        clear_line(session)

        type_text(session, b'echo "abc')
        report.record("hints-off-keeps-diagnostic", session,
                      is_diagnostic_row)
        clear_line(session)

        session.send(b"koshconf set editor.hints.show_live_diagnostics off\r")
        session.wait_until(is_line(""))
        type_text(session, b'echo "abc')
        session.wait_until(is_line('echo "abc'))
        session.pump(0.3)
        report.record("option-off-hides-diagnostic", session,
                      is_without_hint('echo "abc'))
        clear_line(session)

        session.send(b"koshconf set editor.hints.show_command_synopsis on\r")
        session.wait_until(is_line(""))
        type_text(session, b'cat "abc')
        session.pump(0.3)
        report.record("diagnostics-off-keeps-hint", session,
                      is_explained(["cat"]))
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
        if line.strip().startswith(("menu-", "menu/")):
            return rows
        rows.append(line)
    return None


def is_help_wrapped(columns):
    def do_check(screen):
        rows = get_help_rows(screen)
        if rows is None or len(rows) < 2 or MENU_HEADER not in rows[0]:
            return False
        word_count = len(" ".join(rows).split())
        return (word_count == len(MENU_HELP.split())
                and not any(len(line) >= columns
                            or line.strip().startswith(",")
                            for line in rows))
    return do_check



def is_hint_within(columns, words):
    def do_check(screen):
        rows = screen.get_hint_rows()
        return (is_explained(words)(screen)
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

        prompt_width = session.screen.get_prompt_width()
        filler_width = columns - prompt_width - len("echo  menu/menu-") - 2
        session.send(b"echo " + b"x" * filler_width + b" menu/menu-\t")
        report.record("narrow-menu-far-right-keeps-two-help-rows", session,
                      lambda screen: is_help_wrapped(columns)(screen)
                      and len(get_help_rows(screen)) == 2)
        session.send(ESCAPE)
        session.wait_until(is_menu_closed)
        clear_line(session)

        session.send(b"ls ")
        report.record("narrow-hint-rows-fit-the-width", session,
                      is_hint_within(columns, ["ls"]))
        report.record("narrow-hint-rows-indented", session,
                      are_hint_rows_indented)
        session.send(b"-d sub\r")
        report.record("narrow-hint-rows-erased-on-submit", session,
                      lambda screen: get_state(screen) == ("", "")
                      and screen.get_hint_rows() == []
                      and not any(line.strip() == "synopsis"
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
                      and screen.get_hint_header() == "synopsis")
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


NEXT_WORDS = """#!/bin/sh
echo run >> '%s'
case "$1:$3" in
  zznw:zznw) words="zznwfirst" ;;
  zznw:zznwfirst) words="zzalpha zzbeta" ;;
  zznone:zznone) words="zznonefirst" ;;
  *) words="" ;;
esac
for word in $words; do
  case $word in "$2"*) echo "$word" ;; esac
done
"""


LISTING_WORDS = """#!/bin/sh
echo run >> '%s'
for word in %s; do
  echo "$word"
done
"""


def write_counting_words(path, counter, words, template=COUNTING_WORDS):
    with open(path, "w") as handle:
        handle.write(template % (counter, words))
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
                      is_line("cat ctl/$'ZQa\\E]0;PWN\\ax'"))
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


def run_history_preview_checks(binary, directory, report):
    commands = (
        b"echo hist-view-single",
        b"echo 'hist-view-two\nhist-view-two-tail'",
        b"echo 'hist-view-exact\nhist-view-exact-middle\nhist-view-exact-end'",
        b"printf '%s\\n' 'hist-view-long-one\n\nhist-view-long-three\n"
        b"hist-view-hidden-four'",
        b"echo hist-width-" + b"y" * 130,
    )
    with open(os.path.join(directory, "history"), "wb") as history:
        for command in commands:
            history.write(command.replace(b"\\", b"\\\\").replace(b"\n", b"\\n")
                          + b"\n")

    def shows_rows(screen, rows):
        prompt_row = screen.get_prompt_row()
        lines = screen.get_lines()[prompt_row + 1:]
        return (prompt_row >= 0 and bool(lines) and HISTORY_HEADER in lines[0]
                and [line.strip() for line in lines[1:len(rows) + 1]] == rows
                and not any(line.strip() for line in lines[len(rows) + 1:])
                and has_caret_at(screen, len("hist-view")))

    session = Session(binary, directory, directory, rows=8)
    try:
        session.wait_until(is_line(""))
        session.send(b"hist-width" + CTRL_R)
        report.record("history-preview-still-clips-width", session,
                      lambda screen: has_search_row(
                          screen, lambda row: row.startswith("echo hist-width-y")
                          and row.endswith("yyy...")))
        session.send(ESCAPE)
        session.wait_until(is_line("hist-width"))
        clear_line(session)

        session.send(b"hist-view" + CTRL_R)
        first = ["printf '%s\\n' 'hist-view-long-one", "",
                 "hist-view-long-three...", "showing 1-1 of 4"]
        report.record("history-preview-keeps-blank-line-and-marks-overflow",
                      session, lambda screen: shows_rows(screen, first))
        session.send(DOWN)
        report.record("history-preview-scrolls-by-entry", session,
                      lambda screen: shows_rows(screen, [
                          "echo 'hist-view-exact", "hist-view-exact-middle",
                          "hist-view-exact-end'", "echo 'hist-view-two",
                          "hist-view-two-tail'", "showing 2-3 of 4"])
                      and "\xa0" in screen.get_lines()[screen.get_prompt_row() + 4])
        session.send(UP)
        session.wait_until(lambda screen: shows_rows(screen, first))
        session.send(b"\r")
        report.record("history-preview-accepts-the-complete-command", session,
                      lambda screen: any(line.strip() == "hist-view-hidden-four'"
                                         for line in screen.get_lines())
                      and not any(HISTORY_HEADER in line
                                  for line in screen.get_lines())
                      and screen.count_lines("hist-view-hidden-four") == 0)
        session.send(b"\r")
        session.wait_until(lambda screen: any(
            "enter again to run it" in line for line in screen.get_lines()))
        session.send(b"\r")
        report.record("history-preview-runs-the-full-accepted-command", session,
                      lambda screen: screen.count_lines("hist-view-hidden-four") == 1)
        session.wait_until(is_line(""))
        session.wait_for_raw_mode()
        session.send(b"printf 'history-preview-done\\n'; exit\r")
        saw_marker = session.wait_until(
            lambda screen: "history-preview-done" in screen.get_lines())
        if not (did_exit_cleanly(session) and saw_marker):
            report.is_ok = False
            sys.stderr.write("history preview session did not exit cleanly\n")
    finally:
        session.close()


def run_path_preview_checks(binary, directory, report):
    command_directory = os.path.join(directory, "bin")
    preview_directory = os.path.join(directory, "preview")
    child_directory = os.path.join(preview_directory, "pv-dir")
    os.mkdir(command_directory)
    os.makedirs(os.path.join(child_directory, "sub"))
    for name in (".hidden", "Kid.txt", "apple", "z-two"):
        open(os.path.join(child_directory, name), "w").close()
    with open(os.path.join(preview_directory, "pv-image.bin"), "wb") as handle:
        handle.write(bytes.fromhex(
            "89504e470d0a1a0a 0000000d 49484452 00000280 000001e0 0802000000 00000000"))
    with open(os.path.join(preview_directory, "pv-space image.bin"), "wb") as handle:
        handle.write(b"\x7fELF\x02\x01\x01" + bytes(9) + b"\x02\0\x3e\0" + bytes(44))
    os.mkfifo(os.path.join(preview_directory, "pv-pipe"))
    fixed_time = int(time.time()) - 2 * 86400 - 1800
    for name, mode in (("pv-dir", 0o750), ("pv-image.bin", 0o640),
                       ("pv-space image.bin", 0o640), ("pv-pipe", 0o600)):
        path = os.path.join(preview_directory, name)
        os.chmod(path, mode)
        os.utime(path, (fixed_time, fixed_time))
    modified = ", modified " + time.strftime("%Y-%m-%d %H:%M", time.gmtime(fixed_time))
    modified += " (2d ago)"
    directory_stats = "drwxr-x---" + modified
    directory_rows = ("sub/", ".hidden", "Kid.txt", "apple", "z-two")

    dense = os.path.join(directory, "dense")
    os.mkdir(dense)
    for index in range(26):
        path = os.path.join(dense, "item-%02d" % index)
        os.mkdir(path)
        if index == 3:
            for name in ("...", "<empty>"):
                open(os.path.join(path, name), "w").close()
        if index < 2:
            for child in range(15 + index):
                open(os.path.join(path, "entry-%02d" % child), "w").close()
        if index < 4:
            os.chmod(path, 0o750)
            os.utime(path, (fixed_time, fixed_time))

    for index in range(6):
        path = os.path.join(dense, "short-%02d" % index)
        os.mkdir(path)
        if index < 2:
            for child in range(5 + index):
                open(os.path.join(path, "entry-%02d" % child), "w").close()
            os.chmod(path, 0o750)
            os.utime(path, (fixed_time, fixed_time))

    token = "cat preview/pv-"
    open_menu = token.encode() + b"\t"

    def get_preview_rows(screen, prefix="preview/"):
        lines = screen.get_lines()[screen.get_prompt_row() + 1:]
        rows = []
        detail_column = None
        for line in lines:
            match = re.fullmatch(
                r"\s*(" + re.escape(prefix) + r".*?)\s* {2,}(\S.*)", line)
            if match:
                detail_column = match.start(2)
                rows.append(match.groups())
            elif (detail_column is not None and line[:detail_column].isspace()
                  and line[detail_column:].strip()):
                rows.append(("", line[detail_column:]))
            elif (detail_column is not None
                  and line[:detail_column].lstrip().startswith(MENU_FOOTER)
                  and line[detail_column - 1:detail_column] == " "
                  and line[detail_column:].strip()):
                rows.append(("", line[detail_column:]))
        return rows

    def has_directory_preview(screen):
        rows = get_preview_rows(screen)
        return (tuple(right for _, right in rows)
                == directory_rows + (directory_stats,)
                and "pv-dir/" in rows[0][0])

    def has_preview_style(screen, text, is_bold, is_gray=False):
        for row, line in enumerate(screen.get_lines()):
            if row > screen.get_prompt_row() and line.endswith("  " + text):
                start = len(line) - len(text)
                cells = screen.rows[row][start:len(line)]
                return all(cell[1:3] == (is_gray, is_bold) for cell in cells)
        return False

    def has_file_preview(screen, name, description, mode=None):
        rows = get_preview_rows(screen)
        stats = mode + modified if mode is not None else None
        expected = [description] if stats is None else [description, stats]
        return ([right for _, right in rows] == expected
                and name in rows[0][0]
                and has_preview_style(screen, description, True)
                and (stats is None
                     or has_preview_style(screen, stats, False, True)))

    def finish(session):
        if session.screen.get_menu() is not None:
            session.send(ESCAPE)
            session.wait_until(is_menu_closed)
        session.send(CTRL_C)
        session.wait_until(is_line(""))
        session.send(b"printf 'preview-done\\n'; exit\r")
        saw_marker = session.wait_until(
            lambda screen: "preview-done" in screen.get_lines())
        deadline = time.monotonic() + WAIT_SECONDS
        while time.monotonic() < deadline and session.pump(0.02):
            pass
        if (not saw_marker or time.monotonic() >= deadline
                or re.search(rb"(?:ERROR|SUMMARY): .*Sanitizer|runtime error:",
                             session.raw)):
            report.is_ok = False
            sys.stderr.write("preview session did not exit cleanly\n"
                             + repr(bytes(session.raw[-4096:])) + "\n")

    session = Session(binary, directory, command_directory, columns=144,
                      extra_environment={"TZ": "UTC0"})
    try:
        session.wait_until(is_line(""))
        session.send(b"koshconf set completion.add_space_after_completed_word on\r")
        session.wait_until(is_line(""))
        session.send(open_menu)
        report.record("preview-directory-beside-selection", session,
                      has_directory_preview)
        directory_color = b"\x1b[38;5;110msub/"
        report.record("preview-uses-menu-directory-color", session,
                      lambda screen: directory_color in session.raw)
        report.record("preview-stats-are-gray", session,
                      lambda screen: b"\x1b[90m" + directory_stats.encode()
                      in session.raw)

        files = (
            ("preview-file-follows-selection", "pv-image.bin",
             "PNG image, 640x480, 8-bit/channel", "-rw-r-----"),
            ("preview-fifo-without-opening", "pv-pipe", "fifo", "prw-------"),
            ("preview-uses-the-unquoted-path", "pv-space image.bin",
             "ELF executable, 64-bit, x86-64, little-endian", "-rw-r-----"),
        )
        observed = set()

        def observe_selection(screen):
            state = get_state(screen)
            if state is None or state[0] != token:
                return False
            for check, name, description, mode in files:
                is_selected = any(name in line and "\xa0" in line
                                  for line in screen.get_lines()[screen.get_prompt_row() + 1:])
                if (check not in observed and is_selected
                        and has_file_preview(screen, "pv-dir/", description,
                                             mode)):
                    observed.add(check)
                    return True
            return False

        for _ in files:
            session.send(DOWN)
            session.wait_until(observe_selection)
        for check, _, _, _ in files:
            report.record(check, session, lambda screen: check in observed)

        session.send(ESCAPE)
        report.record("preview-clears-on-close", session,
                      lambda screen: is_menu_closed(screen)
                      and not get_preview_rows(screen))
        session.send(CTRL_C)
        session.wait_until(is_line(""))
        session.send(open_menu)
        session.wait_until(has_directory_preview)
        session.send(b"i")
        narrowed = session.wait_until(lambda screen: has_file_preview(
            screen, "pv-image.bin", "PNG image, 640x480, 8-bit/channel"))
        session.send(BACKSPACE)
        def has_restored_preview(screen):
            state = get_state(screen)
            menu = screen.get_menu()
            if state != (token, "dir/") or menu is None:
                return False
            return has_directory_preview(screen)

        widened = session.wait_until(has_restored_preview)
        session.send(RIGHT)
        descended = session.wait_until(
            has_typed_menu(token + "dir/", ["preview/pv-dir/Kid.txt",
                                           "preview/pv-dir/sub/",
                                           "preview/pv-dir/apple",
                                           "preview/pv-dir/z-two"]))
        session.send(CTRL_LEFT)
        report.record("directory-navigation-right-and-left", session,
                      lambda screen: descended and has_typed_menu(
                          "cat preview/", ["preview/pv-dir/", "preview/pv-image.bin",
                                           "preview/pv-pipe",
                                           "preview/'pv-space image.bin'"])(screen)
                      and has_directory_preview(screen))
        session.send(CTRL_RIGHT)
        report.record("directory-navigation-ctrl-right", session,
                      has_typed_menu("cat preview/pv-dir/", [
                          "preview/pv-dir/Kid.txt", "preview/pv-dir/sub/",
                          "preview/pv-dir/apple", "preview/pv-dir/z-two"]))
        session.send(CTRL_LEFT)
        session.wait_until(has_directory_preview)
        for key, offset in ((LEFT, -1), (HOME, -len("cat preview/")),
                            (END, 0)):
            session.send(key)
            moved = session.wait_until(
                lambda screen: screen.get_menu() is not None
                and get_state(screen)[0] == "cat preview/"
                and screen.column == screen.get_prompt_width()
                + len("cat preview/") + offset)
            if not moved:
                break
        report.record("cursor-motion-keeps-path-menu-open", session,
                      lambda screen: moved and has_directory_preview(screen))
        report.record("preview-survives-narrow-and-widen", session,
                      lambda screen: narrowed and widened)

        def has_preview_entry(screen, line):
            return is_typed_with_menu(line)(screen) and has_directory_preview(screen)

        clear_line(session)
        session.send(open_menu)
        session.wait_until(has_directory_preview)
        command_end = len("cat")
        session.send(LEFT * (len(token) - command_end))
        at_command = session.wait_until(
            lambda screen: has_typed_menu(token, ["cat"])(screen)
            and has_caret_at(screen, command_end))
        session.send(RIGHT)
        report.record("right-on-unchanged-command-refreshes-argument-menu",
                      session,
                      lambda screen: at_command
                      and has_preview_entry(screen, token)
                      and has_caret_at(screen, command_end + 1))
        session.send(LEFT)
        at_command = session.wait_until(
            lambda screen: has_typed_menu(token, ["cat"])(screen)
            and has_caret_at(screen, command_end))
        session.send(CTRL_RIGHT)
        report.record("ctrl-right-on-command-moves-by-word", session,
                      lambda screen: at_command
                      and has_preview_entry(screen, token)
                      and has_caret_at(screen, len("cat preview")))

        session.send(LEFT * len(" preview"))
        at_command = session.wait_until(
            lambda screen: has_typed_menu(token, ["cat"])(screen)
            and has_caret_at(screen, command_end))
        # Send both keys together so Ctrl-Right serves the pending argument menu.
        session.send(b" " + CTRL_RIGHT)
        entered_directory = "cat bin/ preview/pv-"
        report.record("ctrl-right-gathers-pending-directory-menu", session,
                      lambda screen: at_command
                      and is_typed_with_menu(entered_directory)(screen)
                      and has_caret_at(screen, len("cat bin/")))

        clear_line(session)
        missing = "cat zzmissing"
        missing_line = missing + " preview/pv-"
        session.send(missing_line.encode() + b"\t")
        session.wait_until(has_directory_preview)
        session.send(LEFT * (len(missing_line) - len(missing)))
        at_missing = session.wait_until(
            lambda screen: has_typed_menu(
                missing_line, [HISTORY_NO_MATCH])(screen)
            and has_caret_at(screen, len(missing)))
        session.send(CTRL_RIGHT)
        report.record("ctrl-right-with-no-candidates-moves-by-word", session,
                      lambda screen: at_missing
                      and has_preview_entry(screen, missing_line)
                      and has_caret_at(screen, len(missing + " preview")))

        file_line = "cat preview/pv-image.bin"
        for name, key in (("right", RIGHT), ("ctrl-right", CTRL_RIGHT)):
            clear_line(session)
            session.send(open_menu)
            session.wait_until(has_directory_preview)
            session.send(b"i")
            at_file = session.wait_until(
                lambda screen: has_file_preview(
                    screen, "pv-image.bin", "PNG image, 640x480, 8-bit/channel"))
            session.send(key)
            report.record(name + "-accepts-a-changed-file-candidate", session,
                          lambda screen: at_file
                          and is_typed_with_menu(file_line)(screen)
                          and has_caret_at(screen, len(file_line)))

        def is_candidate_selected(screen, index):
            row = screen.get_prompt_row() + 2 + index
            lines = screen.get_lines()
            return 0 <= row < len(lines) and "\xa0" in lines[row]

        def get_first_two_candidates(screen):
            # The first rows can arrive before a long menu finishes drawing.
            entries, _ = screen.get_menu()
            return [entry.split("  ", 1)[0].rstrip() for entry in entries[:2]]

        for name, path in (("dot-slash", "./"), ("root", "/"),
                           ("quoted-root", '"/"')):
            clear_line(session)
            line = "cat " + path
            session.send(line.encode() + b"\t")
            opened = session.wait_until(
                lambda screen: is_typed_with_menu(line)(screen)
                and len(screen.get_menu()[0]) > 1
                and is_candidate_selected(screen, 0)
                and has_caret_at(screen, len(line)))
            session.send(DOWN)
            selected_second = session.wait_until(
                lambda screen: is_typed_with_menu(line)(screen)
                and is_candidate_selected(screen, 1)
                and len(screen.get_menu()[0]) > 1
                and has_caret_at(screen, len(line)))
            candidates_before = (get_first_two_candidates(session.screen)
                                 if selected_second else None)
            # Moving back to the first row proves Ctrl-Left kept the second.
            session.send(CTRL_LEFT + UP)
            processed = session.wait_until(
                lambda screen: is_candidate_selected(screen, 0)
                or is_menu_closed(screen))
            session.send(DOWN)
            report.record("ctrl-left-keeps-menu-at-" + name, session,
                          lambda screen: opened and selected_second and processed
                          and is_candidate_selected(screen, 1)
                          and is_typed_with_menu(line)(screen)
                          and get_first_two_candidates(screen) == candidates_before
                          and has_caret_at(screen, len(line)))

        def has_dense_preview(screen, selected, body):
            rows = get_preview_rows(screen, "dense/")
            return (tuple(text for _, text in rows) == body + (directory_stats,)
                    and is_candidate_selected(screen, selected))

        clear_line(session)
        session.send(b"cat dense/item-\t")
        entries = tuple("entry-%02d" % index for index in range(15))
        report.record("preview-shows-exactly-fifteen-entries", session,
                      lambda screen: has_dense_preview(
                          screen, 0, entries))
        report.record("primary-menu-caps-at-sixteen-candidates", session,
                      lambda screen: screen.get_menu() is not None
                      and len(screen.get_menu()[0]) == 16
                      and screen.get_menu()[1] == 26)
        session.send(DOWN)
        overflow = "previewing 15 out of 16"

        def has_aligned_overflow_footer(screen):
            menu = screen.get_menu()
            if menu is None or len(menu[0]) != 16 or menu[1] != 26:
                return False
            lines = screen.get_lines()[screen.get_prompt_row() + 1:]
            last = next((index for index, line in enumerate(lines)
                         if line.lstrip().startswith("dense/item-15/ ")), -1)
            return (0 <= last < len(lines) - 1
                    and re.fullmatch(r"\s*showing 1-16 of 26 +"
                                     + re.escape(directory_stats),
                                     lines[last + 1]) is not None)

        report.record("preview-shows-gray-count-on-directory-overflow", session,
                      lambda screen: has_dense_preview(
                          screen, 1, entries + (overflow,))
                      and has_preview_style(screen, overflow, False, True)
                      and has_aligned_overflow_footer(screen))
        session.send(DOWN)
        report.record("preview-shows-bold-gray-empty-marker", session,
                      lambda screen: has_dense_preview(
                          screen, 2, ("<empty>",))
                      and has_preview_style(screen, "<empty>", True, True))
        session.send(DOWN)
        report.record("preview-keeps-literal-marker-filenames-plain", session,
                      lambda screen: (has_dense_preview(
                          screen, 3, ("...", "<empty>"))
                          or has_dense_preview(
                              screen, 3, ("<empty>", "...")))
                      and all(has_preview_style(screen, marker, False)
                              for marker in ("...", "<empty>")))
        clear_line(session)
        session.send(b"cat dense/short-\t")
        report.record("preview-shows-all-five-entries-without-summary", session,
                      lambda screen: has_dense_preview(screen, 0, entries[:5]))
        session.send(DOWN)
        report.record("preview-keeps-six-entries-independent-of-primary-height",
                      session, lambda screen: has_dense_preview(
                          screen, 1, entries[:6]))
        clear_line(session)
        session.send(b"cat dense/item-\t")
        session.wait_until(lambda screen: has_dense_preview(screen, 0, entries))
        session.send(b"01")
        report.record("preview-shows-fifteen-entries-for-a-sole-directory",
                      session, lambda screen: has_typed_menu(
                          "cat dense/item-01", ["dense/item-01/"])(screen)
                      and has_dense_preview(screen, 0, entries + (overflow,))
                      and has_preview_style(screen, overflow, False, True))
        finish(session)
    finally:
        session.close()

    session = Session(binary, directory, command_directory, columns=144, rows=5)
    try:
        session.wait_until(is_line(""))
        session.send(open_menu + b"d")
        report.record("preview-counts-viewport-overflow", session,
                      lambda screen: has_caret_at(screen, len("cat preview/pv-d"))
                      and get_state(screen)[0] == "cat preview/pv-d"
                      and tuple(text for _, text in get_preview_rows(screen))
                      == directory_rows[:2] + ("previewing 2 out of 5",))
        session.send(ESCAPE)
        session.wait_until(lambda screen: not get_preview_rows(screen))
        finish(session)
    finally:
        session.close()

    session = Session(binary, directory, command_directory, columns=40)
    try:
        session.wait_until(is_line(""))
        session.send(open_menu)
        report.record("preview-yields-on-narrow-terminal", session,
                      lambda screen: any(MENU_HEADER in line
                                         for line in screen.get_lines())
                      and all(any(name in line for line in screen.get_lines())
                              for name in ("pv-dir/", "pv-image.bin", "pv-pipe",
                                           "pv-space image.bin"))
                      and not get_preview_rows(screen))
        finish(session)
    finally:
        session.close()

    links = os.path.join(directory, "links")
    os.mkdir(links)
    for name, target in (("regular", "../preview/pv-image.bin"),
                         ("missing", "missing-target"),
                         ("directory", "../preview/pv-dir"),
                         ("pipe", "../preview/pv-pipe")):
        os.symlink(target, os.path.join(links, name))
        with open(os.path.join(links, name + "-peer"), "w"):
            pass

    session = Session(binary, directory, command_directory, columns=144,
                      extra_environment={"TZ": "UTC0"})
    try:
        session.wait_until(is_line(""))
        for name, target, details in (
                ("regular", "../preview/pv-image.bin",
                 ("PNG image, 640x480, 8-bit/channel", "-rw-r-----" + modified)),
                ("missing", "missing-target", ("dangling target",)),
                ("directory", "../preview/pv-dir", directory_rows + (directory_stats,)),
                ("pipe", "../preview/pv-pipe", ("fifo", "prw-------" + modified))):
            session.send(("cat links/" + name + "\t").encode())
            expected = ("symbolic link -> " + target,) + details
            def has_link_preview(screen):
                rows = screen.get_lines()[screen.get_prompt_row() + 1:]
                for index, row in enumerate(rows):
                    column = row.find("symbolic link -> ")
                    if column >= 0 and "\xa0links/" + name in row:
                        detail_rows = [row[column:]]
                        for line in rows[index + 1:index + len(expected)]:
                            detail_rows.append(re.sub(r"^\s*links/\S+\s+", "",
                                                      line).strip())
                        return tuple(detail_rows) == expected
                return False

            report.record("preview-symbolic-link-" + name, session,
                          has_link_preview)
            clear_line(session)
        finish(session)
    finally:
        session.close()


def main():
    is_path_preview_only = os.environ.get("KOSH_TEST_PATH_PREVIEW_ONLY") == "1"
    is_history_preview_only = os.environ.get("KOSH_TEST_HISTORY_PREVIEW_ONLY") == "1"
    is_completion_keys_only = os.environ.get(
        "KOSH_TEST_COMPLETION_KEYS_ONLY") == "1"
    if sys.platform != "linux" and not (is_path_preview_only
                                        or is_completion_keys_only
                                        or is_history_preview_only):
        print("editor ghost and menu PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)

    if is_history_preview_only:
        report = Report()
        with tempfile.TemporaryDirectory(prefix="kosh-history-pty-") as directory:
            run_history_preview_checks(binary, directory, report)
        return 0 if report.is_ok else 1

    if is_path_preview_only:
        report = Report()
        with tempfile.TemporaryDirectory(prefix="kosh-preview-pty-") as directory:
            run_path_preview_checks(binary, directory, report)
        return 0 if report.is_ok else 1

    if is_completion_keys_only:
        report = Report()
        with tempfile.TemporaryDirectory(prefix="kosh-completion-pty-") as directory:
            os.mkdir(os.path.join(directory, "sub"))
            os.mkdir(os.path.join(directory, "zzlocal"))
            os.mkdir(os.path.join(directory, "menu"))
            for name in ("sub/alpha-beta.txt", "zzlocal/inner.txt",
                         "menu/menu-apple", "menu/menu-banana"):
                open(os.path.join(directory, name), "w").close()
            session = Session(binary, directory, directory)
            try:
                if session.wait_until(is_line("")):
                    record_default_completion_space(session, report)
                    run_path_spacing_checks(session, report)
                    run_completion_key_checks(session, report, directory)
                    session.send(b"printf 'completion-keys-done\\n'; exit\r")
                saw_marker = session.wait_until(
                    lambda screen: "completion-keys-done" in screen.get_lines())
                if not (did_exit_cleanly(session) and saw_marker):
                    sys.stderr.write("completion key session did not exit cleanly\n"
                                     + repr(bytes(session.raw[-4096:])) + "\n")
                    return 1
            finally:
                session.close()
        return 0 if report.is_ok else 1

    directory = tempfile.mkdtemp(prefix="kosh-editor-pty-")
    report = Report()
    try:
        os.makedirs(os.path.join(directory, "sub"))
        os.makedirs(os.path.join(directory, "menu"))
        os.makedirs(os.path.join(directory, "zzlocal"))
        os.makedirs(os.path.join(directory, "zzhollow"))
        open(os.path.join(directory, "zzlocal", "inner.txt"), "w").close()
        os.makedirs(os.path.join(directory, "late"))
        os.makedirs(os.path.join(directory, "later"))
        os.makedirs(os.path.join(directory, "cdpath", "cdpath-target"))
        os.makedirs(os.path.join(directory, "bin"))
        with open(os.path.join(directory, "sub", "alpha-beta.txt"), "w") as handle:
            handle.write("ALPHA-CONTENT\n")
        for name in ("apple", "apricot", "avocado", "banana"):
            open(os.path.join(directory, "menu", "menu-" + name), "w").close()
        for name in ("zzprobe-one", "zzprobe-two", "nproc"):
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
        write_counting_words(os.path.join(directory, "case-words"),
                             os.path.join(directory, "case-runs"),
                             "apple Apricot avocado", LISTING_WORDS)
        write_counting_words(os.path.join(directory, "empty-words"),
                             os.path.join(directory, "empty-runs"),
                             "zzemptyA zzemptyB")
        with open(os.path.join(directory, "next-words"), "w") as handle:
            handle.write(NEXT_WORDS % os.path.join(directory, "next-runs"))
        os.chmod(os.path.join(directory, "next-words"), 0o755)
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
        with tempfile.TemporaryDirectory(prefix="kosh-history-pty-") as history:
            run_history_preview_checks(binary, history, report)
        with tempfile.TemporaryDirectory(prefix="kosh-preview-pty-") as preview:
            run_path_preview_checks(binary, preview, report)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
