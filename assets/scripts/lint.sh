#!/usr/bin/bash

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Records the lint demo: kosh analyzes the 400,000-line configure script of
# Open MPI and reports the time it took.

. "$(dirname -- "$0")/lib.sh"

start lint "$DEMO_HOME/Projects/kosh"

type_text "cd ~/E"
pause 0.4
key Tab
pause 1.0
type_text "op" 0.3
pause 0.8
key Tab
pause 0.8
key Escape
pause 0.4
key Enter
pause 1.2

type_text "wc -l conf"
pause 0.4
key Tab
pause 0.8
type_text "ure" 0.2
pause 0.6
key Escape
key Enter
pause 1.6

type_text "time -R kosh --lint ./conf"
pause 0.4
key Tab
pause 0.8
type_text "ure" 0.2
pause 0.6
key Escape
pause 0.6
key Enter
wait_for "rss" 120
pause 3.0

key C-d
finish
