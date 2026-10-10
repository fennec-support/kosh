#!/usr/bin/env python3
#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Drives Tab through a real PTY on a command whose --help is slow, and checks
# that the documentation load never blocks the editor. A help that answers
# within the idle delay opens the menu with no loading text ever written. A
# slower help shows the loading text under the word and opens the menu when it
# finishes. A key typed during the load lands on the line at once, takes the
# loading text down, and leaves the load running so the next Tab opens the menu
# without forking again. Escape closes the Tab quietly. A completion function
# that takes a while shows the loading text before its menu. A menu of 600
# commands echoes a space and the next word at once and gathers once after the
# typing pauses. Keys inside the pause move it and cost no gather, a return to
# a word already gathered asks the host nothing, and a new word after a short
# list gathers at once, with every key echoed within 50 ms. A Tab on a command with man pages does not queue behind a slow
# hint load, and a key typed while a slow manpath builds the page index lands at
# once. Every wait polls under a deadline, so a failure reports the last screen
# instead of hanging.

import os
import shutil
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from editor_ghost_menu_pty import (  # noqa: E402
    ESCAPE, BACKSPACE, Report, Session, count_marker_lines, get_state,
    is_line, type_text)

SLOW_HELP = """#!/bin/sh
echo $$ >> '%s'
read delay < '%s'
%s "$delay"
echo "Usage: act [command] [flags]"
echo
echo "Flags:"
echo "  -f, --file=FILE   read the workflow from FILE"
"""

LOADING = b"loading..."
FLAGS = ["--file", "-f"]


def has_flag_menu(screen):
    menu = screen.get_menu()
    return (menu is not None
            and sorted(entry.split()[0] for entry in menu[0]) == sorted(FLAGS))


def has_loading_row(screen):
    return any("loading..." in line for line in screen.get_lines())


def has_closed_line(typed):
    return lambda screen: (is_line(typed)(screen)
                           and screen.get_menu() is None
                           and not has_loading_row(screen))


def run_scenario(binary, directory, name, delay, check):
    scenario = os.path.join(directory, name)
    command_directory = os.path.join(scenario, "bin")
    os.makedirs(command_directory)
    marker = os.path.join(scenario, "marker")
    delay_file = os.path.join(scenario, "delay")
    with open(delay_file, "w") as handle:
        handle.write(delay + "\n")
    path = os.path.join(command_directory, "act")
    with open(path, "w") as handle:
        handle.write(SLOW_HELP % (marker, delay_file, shutil.which("sleep")))
    os.chmod(path, 0o755)

    session = Session(binary, scenario, command_directory)
    try:
        if not report.record(name + "-prompt", session, is_line("")):
            return
        check(session, scenario)
    finally:
        session.close()


def check_fast(session, scenario):
    type_text(session, b"act -")
    session.send(b"\t")
    if report.record("fast-help-opens-the-menu", session, has_flag_menu):
        report.record("fast-help-never-draws-loading", session,
                      lambda screen: LOADING not in bytes(session.raw))


def check_slow(session, scenario):
    type_text(session, b"act -")
    session.send(b"\t")
    if report.record("slow-help-draws-loading-first", session, has_loading_row):
        report.record("slow-help-opens-the-menu", session, has_flag_menu)


def check_key(session, scenario):
    type_text(session, b"act -")
    session.send(b"\t")
    if not report.record("key-load-draws-loading", session, has_loading_row):
        return
    session.send(b"Q")
    report.record("key-during-load-lands-on-the-line", session,
                  has_closed_line("act -Q"))
    session.send(BACKSPACE)
    session.wait_until(is_line("act -"))
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        session.pump(0.05)
    session.send(b"\t")
    report.record("next-tab-opens-without-another-fork", session,
                  lambda screen: has_flag_menu(screen)
                  and count_marker_lines(scenario, "marker") == 1)


def check_escape(session, scenario):
    type_text(session, b"act -")
    session.send(b"\t")
    if not report.record("escape-load-draws-loading", session,
                         has_loading_row):
        return
    session.send(ESCAPE)
    report.record("escape-during-load-closes-quietly", session,
                  has_closed_line("act -"))
    session.pump(0.5)
    report.record("escape-leaves-the-line-clean", session,
                  has_closed_line("act -"))


SPEC_WORDS = ["alpha", "beta"]


def has_spec_menu(screen):
    menu = screen.get_menu()
    return (menu is not None
            and sorted(entry.split()[0] for entry in menu[0]) == SPEC_WORDS)


def check_spec(session, scenario):
    with open(os.path.join(scenario, "bin", "nap"), "w") as handle:
        handle.write("#!/bin/sh\n%s 0.6\n" % shutil.which("sleep"))
    os.chmod(os.path.join(scenario, "bin", "nap"), 0o755)
    type_text(session, b"_spec() { nap; COMPREPLY=(alpha beta); }; "
              b"complete -F _spec spec_cmd")
    session.send(b"\r")
    if not report.record("spec-defined", session, is_line("")):
        return
    type_text(session, b"spec_cmd ")
    session.send(b"\t")
    if report.record("slow-spec-draws-loading-first", session,
                     has_loading_row):
        report.record("slow-spec-opens-the-menu", session, has_spec_menu)


BIG_MENU_ECHO_SECONDS = 0.2
BIG_MENU_COMMAND_COUNT = 600
KEY_ECHO_SECONDS = 0.05
INDEX_MENU_SECONDS = 1.5
PAGE_COUNT = 3000


def read_marker_count(scenario):
    return count_marker_lines(scenario, "gathers")


def read_settled_marker_count(session, scenario):
    count = read_marker_count(scenario)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        session.pump(0.3)
        settled = read_marker_count(scenario)
        if settled == count:
            return count
        count = settled
    return count


def timed_echo(session, keys):
    written = len(session.raw)
    started = time.monotonic()
    session.send(keys)
    while len(session.raw) == written and time.monotonic() - started < 2.0:
        session.pump(0.002)
    return time.monotonic() - started


def check_big_menu(session, scenario):
    mark = os.path.join(scenario, "bin", "mark")
    with open(mark, "w") as handle:
        handle.write("#!/bin/sh\necho x >> '%s'\n" %
                     os.path.join(scenario, "gathers"))
    os.chmod(mark, 0o755)
    type_text(session, b"_spec() { mark; COMPREPLY=(alpha beta); }; "
              b"complete -F _spec mz000")
    session.send(b"\r")
    if not report.record("big-menu-spec-defined", session, is_line("")):
        return
    type_text(session, b"mz")
    session.send(b"\t")
    if not report.record(
            "big-menu-opens-over-the-threshold", session,
            lambda screen: (screen.get_menu() is not None
                            and screen.get_menu()[1] is not None
                            and screen.get_menu()[1] >= BIG_MENU_COMMAND_COUNT)):
        return
    session.pump(0.3)
    before = read_marker_count(scenario)
    burst_seconds = timed_echo(session, b"000 a")
    session.pump(0.02)
    report.record("big-menu-space-echoes-at-once", session,
                  lambda screen: burst_seconds < BIG_MENU_ECHO_SECONDS)
    report.record("big-menu-space-does-not-gather-at-once", session,
                  lambda screen: read_marker_count(scenario) == before)
    session.wait_until(is_line("mz000 a"))
    report.record("big-menu-word-echoes-at-once", session,
                  lambda screen: is_line("mz000 a")(screen))
    session.pump(0.6)
    report.record("big-menu-gathers-once-after-the-pause", session,
                  lambda screen: (screen.get_menu() is not None
                                  and sorted(entry.split()[0]
                                             for entry in screen.get_menu()[0])
                                  == SPEC_WORDS
                                  and read_marker_count(scenario) == before + 1))
    session.pump(0.6)
    report.record("big-menu-does-not-gather-again", session,
                  lambda screen: read_marker_count(scenario) == before + 1)
    check_big_menu_word_rules(session, scenario, before + 1)


def check_big_menu_word_rules(session, scenario, base_count):
    slowest = 0.0
    for key in b"xyzw":
        slowest = max(slowest, timed_echo(session, bytes([key])))
        session.pump(0.03)
    session.pump(0.6)
    typed_count = read_settled_marker_count(session, scenario)
    report.record("big-menu-short-list-keys-echo-at-once", session,
                  lambda screen: slowest < KEY_ECHO_SECONDS)
    report.record("big-menu-short-list-asks-for-each-key", session,
                  lambda screen: typed_count == base_count + 1)

    for key in (BACKSPACE * 3):
        timed_echo(session, bytes([key]))
        session.pump(0.05)
    before_return_count = read_settled_marker_count(session, scenario)
    return_seconds = timed_echo(session, BACKSPACE)
    session.pump(0.6)
    report.record("big-menu-returned-word-echoes-at-once", session,
                  lambda screen: return_seconds < KEY_ECHO_SECONDS
                  and is_line("mz000 a", "lpha")(screen))
    report.record("big-menu-returned-word-does-not-ask-the-host", session,
                  lambda screen: read_marker_count(scenario)
                  == before_return_count)

    space_seconds = timed_echo(session, b" ")
    session.pump(0.1)
    report.record("big-menu-new-word-gathers-without-the-pause", session,
                  lambda screen: space_seconds < KEY_ECHO_SECONDS
                  and read_marker_count(scenario) == before_return_count + 1)
    slowest = 0.0
    for key in b"bc":
        slowest = max(slowest, timed_echo(session, bytes([key])))
        session.pump(0.1)
    session.pump(0.6)
    report.record("big-menu-second-pause-echoes-at-once", session,
                  lambda screen: slowest < KEY_ECHO_SECONDS
                  and get_state(screen)[0] == "mz000 a bc")


def build_man_tree(scenario, manpath_delay, man_delay):
    command_directory = os.path.join(scenario, "bin")
    pages = os.path.join(scenario, "man", "man1")
    os.makedirs(pages)
    for name in ("zed", "zed-run", "zed-list"):
        open(os.path.join(pages, name + ".1.gz"), "w").close()
    for index in range(PAGE_COUNT):
        open(os.path.join(pages, "page%04d.1.gz" % index), "w").close()
    sleep = shutil.which("sleep")
    for name, body in (
            ("manpath", "%s %s\necho '%s'\n" % (
                sleep, manpath_delay, os.path.join(scenario, "man"))),
            ("man", "%s %s\n" % (sleep, man_delay)),
            ("zed", "true\n")):
        path = os.path.join(command_directory, name)
        with open(path, "w") as handle:
            handle.write("#!/bin/sh\n" + body)
        os.chmod(path, 0o755)


def has_zed_menu(screen):
    menu = screen.get_menu()
    return (menu is not None
            and sorted(entry.split()[0] for entry in menu[0])
            == ["list", "run"])


def export_manpath(session, scenario):
    type_text(session, ("export MANPATH='%s'" % os.path.join(
        scenario, "man")).encode())
    session.send(b"\r")
    return session.wait_until(is_line(""))


def check_hint_load(session, scenario):
    if not export_manpath(session, scenario):
        return
    type_text(session, b"zed ")
    session.pump(0.6)
    started = time.monotonic()
    session.send(b"\t")
    report.record("tab-does-not-queue-behind-a-hint-load", session,
                  lambda screen: has_zed_menu(screen)
                  and time.monotonic() - started < INDEX_MENU_SECONDS)


def check_slow_index(session, scenario):
    if not export_manpath(session, scenario):
        return
    type_text(session, b"zed ")
    session.send(b"\t")
    if not report.record("slow-index-draws-loading", session, has_loading_row):
        return
    started = time.monotonic()
    session.send(b"Q")
    report.record("key-during-the-index-lands-at-once", session,
                  lambda screen: has_closed_line("zed Q")(screen)
                  and time.monotonic() - started < BIG_MENU_ECHO_SECONDS)
    session.send(BACKSPACE)
    session.wait_until(is_line("zed"))
    session.send(b" ")
    session.send(b"\t")
    report.record("tab-after-the-index-opens-the-menu", session, has_zed_menu)


def run_man_scenario(binary, directory, name, manpath_delay, man_delay, check):
    scenario = os.path.join(directory, name)
    os.makedirs(os.path.join(scenario, "bin"))
    build_man_tree(scenario, manpath_delay, man_delay)
    session = Session(binary, scenario, os.path.join(scenario, "bin"))
    try:
        if not report.record(name + "-prompt", session, is_line("")):
            return
        check(session, scenario)
    finally:
        session.close()


def run_big_menu_scenario(binary, directory):
    scenario = os.path.join(directory, "bigmenu")
    command_directory = os.path.join(scenario, "bin")
    os.makedirs(command_directory)
    for index in range(BIG_MENU_COMMAND_COUNT):
        path = os.path.join(command_directory, "mz%03d" % index)
        with open(path, "w") as handle:
            handle.write("#!/bin/sh\ntrue\n")
        os.chmod(path, 0o755)
    session = Session(binary, scenario, command_directory)
    try:
        if not report.record("bigmenu-prompt", session, is_line("")):
            return
        check_big_menu(session, scenario)
    finally:
        session.close()


report = Report()


def main():
    if sys.platform != "linux":
        print("editor pending Tab PTY probes: skipped (requires Linux)")
        return 0
    binary = os.environ.get("BIN")
    if not binary:
        print("BIN is required", file=sys.stderr)
        return 2
    binary = os.path.abspath(binary)

    directory = tempfile.mkdtemp(prefix="kosh-pending-tab-pty-")
    try:
        run_scenario(binary, directory, "fast", "0.1", check_fast)
        run_scenario(binary, directory, "slow", "0.6", check_slow)
        run_scenario(binary, directory, "key", "1.5", check_key)
        run_scenario(binary, directory, "escape", "1.5", check_escape)
        run_scenario(binary, directory, "spec", "0.1", check_spec)
        run_big_menu_scenario(binary, directory)
        run_man_scenario(binary, directory, "hintload", "0", "3",
                         check_hint_load)
        run_man_scenario(binary, directory, "slowindex", "2", "0",
                         check_slow_index)
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
