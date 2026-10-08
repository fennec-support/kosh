#!/bin/bash
# An arithmetic expansion error abandons the rest of the input line, every
# command of it, and $? is 1 on the next line. A subscript, a substring
# offset, and an integer attribute assignment do the same. The (( )), let,
# [[ ]], and for (( )) commands fail with status 1 instead, unless the error
# comes from a subscript or an expansion inside them. A subscript or integer
# assignment error abandons the line of the outermost script even inside an
# eval or a sourced file, and it ends a -c string or a command substitution
# with status 1.
echo a $(( 1 + )) b; echo same
echo "list=$?"
true && echo $(( 1 + )) || echo or; echo same
echo "and-or=$?"
f() { echo in; echo $(( 1 + )); echo after; }
f; echo same
echo "function=$?"
g() {
  echo body
  echo $(( 1 + ))
  echo after
}
g
echo "multi-line-function=$?"
for i in 1 2; do echo "i$i"; echo $(( 1 + )); echo after; done; echo same
echo "loop=$?"
for i in 1 2; do
  echo "i$i"
  echo $(( 1 + ))
  echo after
done
echo "multi-line-loop=$?"
{ echo grouped; echo $(( 1 + )); echo after; }; echo same
echo "group=$?"
if echo $(( 1 + )); then echo then; else echo else; fi; echo same
echo "if=$?"
case $(( 1 + )) in *) echo matched ;; esac; echo same
echo "case=$?"
x=$(( 1 + )); echo same
echo "assignment=$?"
x=$(( 1 + )) echo prefix; echo same
echo "prefix=$?"
echo "${unset_name:-$(( 1 + ))}"; echo same
echo "default=$?"
true; echo first; echo $(( 1 + )) &&
echo continued
echo "continuation=$?"
echo $(( 1 + )); echo same \
  continued
echo "backslash=$?"
echo $(( 1 + )); echo same # comment
echo "comment=$?"
echo $(( 1 + ));
echo "semicolon-newline=$?"

x=$(echo before; echo $(( 1 + )); echo after); echo "substitution=[$x] $?"
y=$(
echo first
echo $(( 1 + ))
echo after
); echo "multi-line-substitution=[$y] $?"
( echo sub; echo $(( 1 + )); echo after ); echo "subshell=$?"
(
echo sub
echo $(( 1 + ))
echo after
); echo "multi-line-subshell=$?"
echo $(( 1 + )) | cat; echo "pipeline=$? ${PIPESTATUS[*]}"
echo $(( 1 + )) & echo "background=$?"
wait

eval 'echo a; echo $(( 1 + )); echo b'; echo "eval=$?"
eval 'echo a; echo $(( 1 + )); echo b
echo "inside=$?"'; echo "multi-line-eval=$?"
source_file=${TMPDIR:-/tmp}/arithmetic_error_discards_line.$$
printf '%s\n' 'echo a; echo $(( 1 + )); echo b' 'echo "sourced=$?"' \
  >"$source_file"
. "$source_file"; echo "source=$?"
rm -f "$source_file"

trap 'echo trapped' ERR
h() { echo $(( 1 + )); }
h; echo same
echo "no-err-trap=$?"
trap - ERR
set -e
echo $(( 1 + )); echo same
echo "errexit=$?"
set +e

(( 1 + )); echo "arithmetic-command=$?"
let '1 +'; echo "let=$?"
[[ 1+ -eq 1 ]]; echo "conditional=$?"
for ((i = 0; i < 1 +; i++)); do :; done; echo "c-for=$?"
cat <<EOF; echo "heredoc=$?"
$(( 1 + ))
EOF

s=abc
echo ${s:1+}; echo same
echo "offset=$?"
echo "${s:1:1+}"; echo same
echo "length=$?"
arr=(1 2)
echo ${arr[1+]}; echo same
echo "subscript=$?"
arr[1+]=3; echo same
echo "subscript-assignment=$?"
echo ${#arr[1+]}; echo same
echo "element-length=$?"
unset 'arr[1+]'; echo same
echo "unset=$?"
(( arr[1+] )); echo same
echo "subscript-in-arithmetic=$?"
let 'arr[1+]'; echo same
echo "subscript-in-let=$?"
(( $(( 1 + )) )); echo same
echo "expansion-in-arithmetic=$?"
declare -i n; n='1 +'; echo same
echo "integer=$?"

eval 'arr[1+]=1; echo a'; echo "subscript-eval=$?"
echo "after-subscript-eval=$?"
eval 'echo ${arr[1+]}; echo a'; echo "subscript-expansion-eval=$?"
eval 'n="1 +"; echo a'; echo "integer-eval=$?"
eval 'echo ${arr[$(( 1 + ))]}; echo a'; echo "inner-expansion-eval=$?"
printf '%s\n' 'arr[1+]=1; echo a' 'echo "sourced-subscript"' >"$source_file"
. "$source_file"; echo "subscript-source=$?"
rm -f "$source_file"
link_dir=$(mktemp -d)
ln -s "$BASH" "$link_dir/bash"
"$link_dir/bash" -c 'echo start; arr[1+]=1 >/dev/null; echo same
echo next'; echo "command-string=$?"
"$link_dir/bash" -c 'arr[1+]=1; echo same
echo next'; echo "command-string-plain=$?"
"$link_dir/bash" -c 'echo $(( 1 + )); echo same
echo next'; echo "command-string-expansion=$?"
rm -r "$link_dir"
x=$(arr[1+]=1; echo in
echo more); echo "subscript-substitution=[$x] $?"

echo $(( "1" + 2 )) "$(( "1 + 2" * 3 ))" $(( "0x1""0" ))
(( "1" )) && echo "quoted-command=$?"
s=abc
echo "${s:"1":"1"}" "${arr["0"]}"
quoted='"1"'
echo $(( quoted )); echo same
echo "quoted-value=$?"
echo $(( '1' )); echo same
echo "single-quoted=$?"
let 'x = "1"'; echo "quoted-let=$?"

( set -o posix; echo $(( 1 + )); echo same ); echo "posix=$?"
( set -o posix; eval 'echo $(( 1 + ))'; echo same ); echo "posix-eval=$?"
( set -o posix; z=$(echo $(( 1 + )); echo b); echo "posix-inner=[$z] $?" )
echo "posix-substitution=$?"
( set -o posix; (( 1 + )); echo "posix-arithmetic-command=$?" )
( set -o posix; echo $(( 1 + )) | cat; echo "posix-pipeline=$?" )
( set -o posix; cat <<EOF
$(( 1 + ))
EOF
echo "posix-heredoc=$?" )
set -o posix
echo ${s:1+}; echo same
echo "posix-offset=$?"
echo ${arr[1+]}; echo same
echo "posix-subscript=$?"
echo end
echo $(( 1 + ))
echo not reached
