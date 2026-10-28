#!/usr/bin/bash

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Records the selector demo: a synopsis that explains every flag of a tar
# command, brew command, flag, and formula completion, path completion with
# arrows and Tab, a command, history search, and multiple cursors that
# replace-regexp edits before the buffer runs. Tab opens a menu
# or accepts its highlighted row, and an open menu follows the typed text.

. "$(dirname -- "$0")/lib.sh"

start selector "$DEMO_HOME"

type_text "tar -xzvf release.tar.gz -C ~/Downloads --strip-components=1 --exclude='*.log'"
wait_for "Exclude files" 15
pause 4.0
key C-a
pause 0.8
key C-k
pause 1.0

type_text "bre" 0.15
pause 0.9
type_text "w "
pause 0.4
key Tab
wait_for "showing 1-16 of" 15
pause 1.6
type_text "inf" 0.3
pause 1.2
keys_slow 0.2 BSpace BSpace BSpace
pause 1.2
type_text "ins" 0.3
pause 1.2
key Tab
pause 1.0

type_text " --" 0.2
wait_for "of 51" 15
pause 1.2
keys_slow 0.35 Down Down Down Down Down
pause 0.8
key Tab
pause 0.8
type_text " "
wait_for "of 16" 15
pause 1.8
type_text "wget" 0.3
pause 1.2
key Tab
pause 1.0
key Escape
pause 0.4
key C-e C-u
pause 0.8

type_text "/o"
pause 0.4
key Tab
wait_for "/opt/homebrew/" 15
pause 1.0
keys_slow 0.35 Down Down Down Down Down
pause 0.6
key Tab
wait_for "/opt/homebrew/Cellar/" 15
pause 1.0
keys_slow 0.35 Down Down Down Down
pause 0.6
key Tab
wait_for "cowthink" 15
pause 1.0
type_text "co" 0.3
pause 1.2
keys_slow 0.5 Down Up
pause 0.6
key Tab
pause 0.6
key Escape
type_text " hello"
pause 0.6
key Enter
pause 2.2

key C-r
pause 1.6
type_text "tar" 0.25
pause 1.4
keys_slow 0.15 BSpace BSpace BSpace
type_text "docker" 0.2
pause 1.6
key Escape
pause 1.0

type_text "cowsay meow"
pause 0.8
key S-Home
pause 1.2
key C-v
pause 0.8
key Down
pause 0.8
key Down
pause 1.0
key End
pause 0.6
type_text " :3" 0.15
pause 1.4
key Enter
pause 1.0
key M-x
pause 1.0
type_text "replace-regexp" 0.1
pause 0.6
key Enter
pause 1.0
type_text "say" 0.2
pause 0.6
key Enter
pause 1.0
type_text "think" 0.2
pause 0.6
key Enter
wait_for "cowthink meow" 10
pause 1.6
key Enter
pause 0.6
key Enter
pause 3.0

key C-d
finish
