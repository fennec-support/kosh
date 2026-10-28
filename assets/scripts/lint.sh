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
sleep 0.4
key Tab
sleep 1.0
type_text "op" 0.3
sleep 0.8
key Tab
sleep 0.8
key Escape
sleep 0.4
key Enter
sleep 1.2

type_text "wc -l conf"
sleep 0.4
key Tab
sleep 0.8
type_text "ure" 0.2
sleep 0.6
key Escape
key Enter
sleep 1.6

type_text "time -R kosh --lint ./conf"
sleep 0.4
key Tab
sleep 0.8
type_text "ure" 0.2
sleep 0.6
key Escape
sleep 0.6
key Enter
wait_for "rss" 120
sleep 3.0

key C-d
finish
