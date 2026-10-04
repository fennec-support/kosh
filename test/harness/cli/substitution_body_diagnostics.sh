dir=$(mktemp -d) || exit 1
trap 'cd / && [ -n "$dir" ] && "$BIN_DIR/invoke-koshkit" rm -rf "$dir"' EXIT
cd "$dir" || exit 1

check()
{
  local name=$1
  printf '%s\n' "== $name"
  "$BIN" --lint "$name" 2>&1
  printf 'status=%s\n' "$?"
}

printf '#!/bin/bash\necho "$(cat f | grep a)"\nx=$(ls *.txt)\necho "$x"\n' \
  > command.sh
printf '#!/bin/bash\ny=`cat f | grep a`\necho "$y"\n' > backtick.sh
printf '#!/bin/bash\ndiff <(cat a | sort) >(cat b | sort)\n' > process.sh
printf '#!/bin/bash\nx=${ cat f | grep a; }\necho "$x"\n' > funsub.sh
printf '#!/bin/bash\ncat <<EOF\nvalue: $(cat f | grep a)\nEOF\n' > heredoc.sh
printf '#!/bin/bash\necho "${x:-$(cat f | grep a)}"\n' > modifier.sh
printf '#!/bin/bash\necho $(( $(cat f | wc -l) + 1 ))\n' > arithmetic.sh
printf '#!/bin/bash\n(( total = $(cat f | wc -l) ))\n' > arithmetic_command.sh
printf '#!/bin/bash\nfor ((i = 0; i < $(cat f | wc -l); i++)); do :; done\n' \
  > cstyle_for.sh
printf '#!/bin/bash\ncase "$(cat f | head -1)" in a) : ;; esac\n' > case.sh
printf '#!/bin/bash\n[[ "$(cat f | head -1)" == a ]]\n' > conditional.sh
printf '#!/bin/bash\nfor i in $(cat f | head -1); do : "$i"; done\n' \
  > for_words.sh
printf '#!/bin/bash\necho "$(echo "$(cat f | grep a)")"\n' > nested.sh
printf '#!/bin/bash\necho hi > "$(cat f | head -1)"\n' > redirect.sh
printf '#!/bin/bash\n# shellcheck disable=SC2002\necho "$(cat f | grep a)"\n' \
  > disabled_file.sh
printf '#!/bin/bash\necho start\n# shellcheck disable=SC2002\nx=$(cat f | grep a)\ny=$(cat f | grep a)\necho "$x$y"\n' \
  > disabled_next.sh
{
  printf '#!/bin/bash\n'
  printf 'f() {\n'
  printf '  local z=$(cat f | grep a)\n'
  printf '  export y=$(cat f | grep b)\n'
  printf '  declare -r w=$(cat f | grep c)\n'
  printf '  v=$(cat f | grep d) true\n'
  printf '  arr=($(cat f | grep e))\n'
  printf '  echo "$z$y$w" "${arr[@]}"\n'
  printf '}\n'
  printf 'f\n'
} > declaration.sh
{
  printf '#!/bin/bash\n'
  printf 'parent=1\n'
  printf 'echo "$(echo "$parent")"\n'
  printf 'echo "$(echo "$missing")"\n'
  printf 'x=$(inner=1; echo "$inner")\n'
  printf 'echo "$inner"\n'
  printf 'y=${ shared=2; echo ok; }\n'
  printf 'echo "$x$y$shared"\n'
} > scope.sh
{
  printf '#!/bin/bash\n'
  printf 'helper() { echo ok; }\n'
  printf 'value=$(helper)\n'
  printf 'echo "$value"\n'
} > function_call.sh
{
  printf '#!/bin/bash\n'
  printf 'x=$(< file)\n'
  printf 'y=`<file`\n'
  printf 'echo "$x$y"\n'
} > bare_read.sh
{
  printf '#!/bin/bash\n'
  printf 'echo "$(printf "%%s\\n" a)"\n'
  printf 'echo "$(printf %%s "x" )" "$(echo y)"\n'
} > escapes.sh

check command.sh
check backtick.sh
check process.sh
check funsub.sh
check heredoc.sh
check modifier.sh
check arithmetic.sh
check arithmetic_command.sh
check cstyle_for.sh
check case.sh
check conditional.sh
check for_words.sh
check nested.sh
check redirect.sh
check disabled_file.sh
check disabled_next.sh
check declaration.sh
check scope.sh
check function_call.sh
check bare_read.sh
check escapes.sh
