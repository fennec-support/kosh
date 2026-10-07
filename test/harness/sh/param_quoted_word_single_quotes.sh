#!/bin/sh
# The word of a ${v-word} family expansion in double quotes or a here-document
# keeps single quotes and a backslash before them, checked against dash. A
# double-quoted part loses its quotes, $'...' stays literal, a single quote
# does not hide the closing brace, an unquoted expansion removes the quotes,
# and the case word and pattern keep the quotes as text.
u=
s=set
printf '<%s>\n' "${u:-'x'}" "${u-'x'}" "${s:+'x'}" "${s+'x'}"
printf '<%s>\n' "${u:='x'}" "$u"
unset u
printf '<%s>\n' "${u='x'}" "$u"
u=
printf '<%s>\n' "${u:-\'x}" "${u:-"a b"}" "${u:-"'x'"}" "${u:-a\"b}"
printf '<%s>\n' "${u:-\}}" "${u:-\x}" "${u:-'}'}" "${u:-'}" "${u:-a\\'b}"
printf '<%s>\n' "${u:-'$s'}" "${u:-'*'}" "${u:-$'a\tb'}" "pre${u:-'x'}post"
printf '<%s>\n' ${u:-'x'} ${u:-"'x'"} ${u:-\'}
printf '<%s>\n' "${u:-${b:-'x'}}" "${s:+${u:-'y'}}" ${s:+"${u:-'y'}"}
set -- "a b" c
printf '<%s>\n' "${u:-'x'"$@"}" "${u:-'$@'}"
set --
cat <<EOF
${u:-'x'} ${u:-"y"} ${u:-\}} ${u:-$'a\tb'}
EOF
case "${u:-'x'}" in
  "'x'") echo case-word-quoted ;;
  *) echo case-word-other ;;
esac
case x in
  "${u:-'x'}") echo case-pattern-match ;;
  *) echo case-pattern-literal ;;
esac
assigned="${u:-'a b'}"
plain=${u:-'a b'}
printf '<%s>\n' "$assigned" "$plain"
