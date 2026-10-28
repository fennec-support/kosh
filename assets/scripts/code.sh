#!/usr/bin/bash

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Records the code demo: Neovim with the kosh language server formats a messy
# script, completes a variable and a function, and lists the symbols. The
# completion menu of the Neovim configuration opens on Ctrl-Space.

. "$(dirname -- "$0")/lib.sh"

start code "$DEMO_HOME/Projects/kosh/examples"

type_text "nvim m"
pause 0.4
key Tab
pause 0.8
key Escape
pause 0.4
key Enter
wait_for "esac" 20
pause 2.5

key o
type_text "# hitting <Space>ff" 0.09
pause 0.6
key Escape
pause 0.8
keys_slow 0.15 Space f f
pause 3.0

key G
pause 0.4
key o
type_text "release" 0.15
pause 0.8
type_text "_target=produc" 0.12
pause 0.8
type_text "tion" 0.12
pause 0.6
key Escape
pause 0.4
key o
type_text "deploy" 0.15
key C-Space
pause 1.6
type_text " \$rel" 0.2
key C-Space
pause 1.6
key Tab
pause 0.8
key Escape
pause 2.5

keys_slow 0.2 g O
pause 3.0

key Escape
type_text ":qa!" 0.1
key Enter
pause 1.5

key C-d
finish
