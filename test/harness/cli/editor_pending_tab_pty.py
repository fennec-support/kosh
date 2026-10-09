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
# that takes a while shows the loading text before its menu. Every wait polls under
# a deadline, so a failure reports the last screen instead of hanging.

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
    finally:
        shutil.rmtree(directory, ignore_errors=True)
    return 0 if report.is_ok else 1


if __name__ == "__main__":
    sys.exit(main())
