#!/bin/sh

printf '== the kosh mood stores as well:\n'
"$BIN" -c 'compgen -V stored -W "alpha" -- a
declare -p stored'

printf '== an invalid name is refused:\n'
"$BIN" -M bash -c 'compgen -V "stored[0]" -W "alpha" -- a
printf "status=%s\n" "$?"'

printf '== an associative target is refused:\n'
"$BIN" -M bash -c 'declare -A stored
compgen -V stored -W "alpha" -- a
printf "status=%s\n" "$?"'

printf '== a read only target is refused:\n'
"$BIN" -M bash -c 'readonly stored=1
compgen -V stored -W "alpha" -- a
printf "status=%s\n" "$?"'

printf '== a missing option value is a usage error:\n'
"$BIN" -M bash -c 'compgen -W "alpha" -V'
printf 'status=%s\n' "$?"
