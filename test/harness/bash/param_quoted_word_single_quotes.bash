#!/bin/bash
# The word of a ${v-word} family expansion in double quotes or a here-document
# keeps single quotes and a backslash before them, checked against bash. A
# double-quoted part loses its quotes, $'...' decodes only inside double
# quotes, an unquoted expansion removes the quotes, and the pattern operators
# keep treating quotes as quoting. Posix mode keeps $'...' literal inside double
# quotes.
u=
s=set
printf '<%s>\n' "${u:-'x'}" "${u-'x'}" "${s:+'x'}" "${s+'x'}"
printf '<%s>\n' "${u:='x'}" "$u"
unset u
printf '<%s>\n' "${u='x'}" "$u"
u=
printf '<%s>\n' "${u:-\'x}" "${u:-"a b"}" "${u:-"'x'"}" "${u:-a\"b}"
printf '<%s>\n' "${u:-\}}" "${u:-\x}" "${u:-'}'}" "${u:-'$s'}" "${u:-'*'}"
printf '<%s>\n' "${u:-$'a\tb'}" "pre${u:-'x'}post" "${u:-$(echo "'x'")}"
printf '<%s>\n' ${u:-'x'} ${u:-"'x'"} ${u:-\'}
printf '<%s>\n' "${u:-${b:-'x'}}" "${s:+${u:-'y'}}" ${s:+"${u:-'y'}"}
printf '<%s>\n' "${s#'s'}" "${s%'t'}" "${s/'e'/'E'}"
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
[[ "${u:-'x'}" == "'x'" ]] && echo conditional-quoted
[[ x == "${u:-'x'}" ]] || echo conditional-literal
[[ ${u:-'x'} == x ]] && echo conditional-unquoted
assigned="${u:-'a b'}"
plain=${u:-'a b'}
declare -a list=("${u:-'x'}")
printf '<%s>\n' "$assigned" "$plain" "${list[0]}"
declare -A table=([x]=bare ["'x'"]=quoted)
printf '<%s>\n' "${table[${u:-'x'}]}" "${table["${u:-'x'}"]}"
set -o posix
printf '<%s>\n' "${u:-$'a\tb'}" ${u:-$'a\tb'} "${u:-'x'}"
