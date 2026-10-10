#!/bin/bash
# A descriptor, signal, or mask past its range is an error instead of a
# wrapped small number, and read, printf -v, mapfile, and getopts refuse a
# target that is not a variable name, all as in bash.
echo a | { read -u 4294967296 x 2> /dev/null; echo "read-fd=$? x=${x-unset}"; }
printf 'a\nb\n' | { mapfile -u 4294967296 lines 2> /dev/null; echo "mapfile-fd=$? count=${#lines[@]}"; }
trap 'echo caught' 4294967298 2> /dev/null
echo "trap-signal=$?"
trap -p INT
saved_mask=$(umask)
umask 77777777777 2> /dev/null
echo "umask=$? mask-kept=$([ "$(umask)" = "$saved_mask" ] && echo yes)"
printf -v 'a-b' '%s' x 2> /dev/null
echo "printf-target=$?"
printf -v 'slot[1]' '%s' kept
echo "printf-element=$? ${slot[1]}"
read -r 'a-b' <<< x 2> /dev/null
echo "read-target=$?"
mapfile 'a b' <<< x 2> /dev/null
echo "mapfile-target=$?"
getopts a '1x' -a 2> /dev/null
echo "getopts-target=$?"
printf '%u %x %o\n' 18446744073709551615 0xffffffffffffffff 01777777777777777777777
echo "printf-unsigned=$?"
printf '%u\n' 18446744073709551616 2> /dev/null
echo "printf-unsigned-overflow=$?"
printf '%d\n' 18446744073709551615 2> /dev/null
echo "printf-signed-overflow=$?"
