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
pause 0.4
key Tab
pause 0.8
key Escape
pause 0.4
key Enter
wait_for "alpine" 20
pause 3.0

key C-b
pause 1.4
keys_slow 0.5 j j
key Enter
pause 1.5
key C-b
pause 0.4
key q
pause 2.0

type_text ":e script.bash" 0.08
pause 2.0
key Enter
wait_for "script.bash" 10
pause 2.5

key i
type_text "#!/bin/s" 0.15
key C-Space
pause 1.6
type_text "h"
pause 0.4
key Escape
pause 0.4
key o
key Enter
type_text "[[ only in bash ]]" 0.1
pause 0.4
key Escape
pause 3.0

type_text ":e .github/workflows/ci.yml" 0.06
pause 2.0
key Enter
wait_for "ubuntu-latest" 10
pause 2.5

keys_slow 0.2 9 G
key o
type_text "print" 0.15
key C-Space
pause 1.6
type_text "f" 0.15
pause 0.6
type_text " '%s\n' \$archiv" 0.12
key C-Space
pause 1.6
key Tab
pause 0.6
key Enter
type_text "docker bu" 0.15
key C-Space
pause 1.6
key Tab
pause 0.8
type_text " --" 0.12
key C-Space
pause 1.6
key Down
pause 3.0

key Escape
pause 0.3
key Escape
type_text ":qa!" 0.1
key Enter
pause 1.5

key C-d
finish
