#!/bin/bash
# ${name@P} decodes the prompt backslash escapes before it expands
# parameters and substitutions, as bash does. Control bytes in the template
# or in an expanded value stay as they are, a backslash that a parameter
# yields is not decoded again, and an escape value such as \W is not
# expanded again.
cd "$(mktemp -d)" || exit 1
HOME=/home/prompt
show() {
  printf '%s: %q\n' "$1" "${2@P}"
}

show soh-stx-etx $'a\001b\002c\003d'
show raw-markers $'\001\033[1m\002x\001\033[0m\002'
show raw-markers-escaped $'\001\\e[1m\002x'
show bracket-markers '\[x\]y\[z'
show dollar-escape '\$HOME'
show backslash-escape '\\$HOME'
show backquote-escape '\`x\`'
show octal-dollar '\044HOME'
show trailing-backslash 'a\'
show backslash-newline $'a\\\nb'
show unknown-escape '\q\z'
x='\u' show value-escape '$x'
x='$HOME' show value-dollar '$x'
v='C:\new\Users' show value-path '$v'
show substitution-escape '$(printf "%s" "\\$HOME")'
mkdir 'd$(echo bad)`echo tick`' && cd 'd$(echo bad)`echo tick`' || exit 1
show directory-name '\W'
show directory-dollar '\W$HOME'
