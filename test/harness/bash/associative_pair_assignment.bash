#!/bin/bash
# An associative array list with no [key]= element alternates keys and
# values, as bash 5.1 and later accept it, and an odd last key gets an empty
# value. declare, local, a plain assignment, and += take the same form, a
# later duplicate key wins, and the output of ${m[@]@K} evaluates back to the
# same array. A list that starts with [key]= must use it for every element.
show() {
  local key
  printf '%s:' "$1"
  while IFS= read -r key; do
    printf ' [%s]=<%s>' "$key" "${x[$key]}"
  done < <(printf '%s\n' "${!x[@]}" | sort)
  echo " (${#x[@]})"
}

declare -A x=(k1 v1 k2 v2); show pairs
declare -A x=(k1 v1 k2); show odd
unset x; declare -A x; x=(a b c d); show assignment
x+=(e f); show append
unset x; declare -A x=(a [b]=2); show "literal value"
unset x; declare -A x=("a b" "c d" '$(echo no)' '`x`'); show quoted
unset x; declare -A x=(k1 v1 k1 v2); show duplicate
unset x; declare -A x=(a); show "one key"
unset x; declare -Ai x=(a 1+1 b '3*2' c); show integer
unset x; declare -Ai x=([a]=1+1); x+=(b 2*5); show "integer append"
f() { local -A x=(p q r s); show local; }
unset x; f

declare -A m=([one]=1 ["t w"]="x y" ['$(touch pwned)']='`touch pwned`' [e]=)
cd "$(mktemp -d)" || exit 1
unset x; declare -A x; eval "x=(${m[@]@K})"; show "@K round trip"
unset x; eval "declare -A x=(${m[*]@K})"; show "@K declare round trip"
ls

unset x; declare -A x=([a]=1 [b]=2); show subscripts
(declare -A x=([a]=1 b 2); echo "not reached")
echo "mixed status $?"
