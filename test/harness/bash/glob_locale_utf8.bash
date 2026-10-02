#!/bin/bash
# Bash pattern matching under the character locale, checked byte-for-byte
# against bash. A ? and the retry of a * consume one whole character in a UTF-8
# locale and one byte in the C locale. Covers the first nonempty of LC_ALL,
# LC_CTYPE, and LANG, assignment, unset, and trims, case, [[ ]], substitution,
# and pathname expansion over multibyte values.

v='日本.語.x'

show() {
  printf '[%s|%s]\n' "${v%?.*}" "${v#*??}" | od -An -c
}

LC_ALL=C.UTF-8
show
LC_ALL=C
show
LC_ALL=en_US.UTF-8
show
LC_ALL=en_US.utf8
show
LC_ALL=C
show

LC_ALL=
LC_CTYPE=C.UTF-8
show
LC_CTYPE=C
LANG=C.UTF-8
show
LC_CTYPE=
show
LANG=POSIX
show
unset LC_ALL LC_CTYPE LANG
show

LC_ALL=C.UTF-8
e='é'
for subject in "$e" 'ab' 'é.' '日'; do
  case $subject in
    ?) echo "case-one:$subject" ;;
    ??) echo "case-two:$subject" ;;
    *) echo "case-other:$subject" ;;
  esac
done
[[ $e == ? ]] && echo cond-one-utf8 || echo cond-one-bytes
[[ $e == ?? ]] && echo cond-two-utf8 || echo cond-two-bytes
[[ $e == ?*? ]] && echo cond-star-utf8 || echo cond-star-bytes
[[ $e != ? ]] && echo cond-ne-utf8 || echo cond-ne-bytes
LC_ALL=C
[[ $e == ? ]] && echo cond-one-utf8 || echo cond-one-bytes
[[ $e == ?? ]] && echo cond-two-utf8 || echo cond-two-bytes
[[ $e == ?*? ]] && echo cond-star-utf8 || echo cond-star-bytes
case $e in
  ?) echo case-c-one ;;
  ??) echo case-c-two ;;
esac

LC_ALL=C.UTF-8
w='éa語b'
echo "${w//?/.}"
echo "${w/?/.}"
echo "${w/#?/.}"
echo "${w/%?/.}"
echo "${w##*?}|${w#?}|${w%%?*}|${w%?}"
LC_ALL=C
echo "${w//?/.}" | od -An -c

LC_ALL=C.UTF-8
bad=$'a\xffb\xc3'
printf '%s' "${bad//?/.}" | od -An -c
printf '%s' "${bad#?}" | od -An -c
printf '%s' "${bad%?}" | od -An -c

dir=$(mktemp -d)
: > "$dir/é"
: > "$dir/x"
: > "$dir/語"
LC_ALL=C.UTF-8
for f in "$dir"/?; do echo "utf8:${f##*/}"; done
LC_ALL=C
for f in "$dir"/?; do echo "bytes:${f##*/}"; done
for f in "$dir"/??; do echo "bytes2:${f##*/}"; done
LC_ALL=C.UTF-8 true
for f in "$dir"/?; do echo "bytes-after-prefix:${f##*/}"; done
rm -rf "$dir"
