#!/usr/bin/bash

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# Records the language server demo: Neovim shows kosh diagnostics in a
# Dockerfile, an Ansible playbook, and a shell script, and completes commands,
# variables, and flags in a GitHub workflow. The file tree opens on Ctrl-B, and
# the completion menu opens on Ctrl-Space.

. "$(dirname -- "$0")/lib.sh"

start lsp "$DEMO_HOME/Projects/kosh/examples"

type_text "nvim Do"
sleep 0.4
key Tab
sleep 0.8
key Escape
sleep 0.4
key Enter
wait_for "alpine" 20
sleep 3.0

key C-b
sleep 1.4
keys_slow 0.5 j j
key Enter
sleep 1.5
key C-b
sleep 0.4
key q
sleep 2.0

type_text ":e script.bash" 0.08
key Enter
wait_for "script.bash" 10
sleep 2.5

key i
type_text "#!/bin/s" 0.15
key C-Space
sleep 1.6
type_text "h"
sleep 0.4
key Escape
sleep 0.4
key o
key Enter
type_text "[[ only in bash ]]" 0.1
sleep 0.4
key Escape
sleep 3.0

type_text ":e .github/workflows/ci.yml" 0.06
key Enter
wait_for "ubuntu-latest" 10
sleep 2.5

keys_slow 0.2 9 G
key o
type_text "print" 0.15
key C-Space
sleep 1.6
type_text "f" 0.15
sleep 0.6
type_text " '%s\n' \$archiv" 0.12
key C-Space
sleep 1.6
key Tab
sleep 0.6
key Enter
type_text "dock" 0.15
sleep 1.0
type_text "er build --" 0.12
key C-Space
sleep 1.6
key Down
sleep 3.0

key Escape
sleep 0.3
key Escape
type_text ":qa!" 0.1
key Enter
sleep 1.5

key C-d
finish
