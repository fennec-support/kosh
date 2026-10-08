cd "$(mktemp -d)" || exit 1
declare -A m=([plain]=1 [a-b.c@d]=2 ["x y"]=3 [*]=4 ["~u"]=5 ["a=b"]=6 ["q\"r"]=8 ["a\\b"]=9 ["#h"]=10 ["t	ab"]=11)
k='$(touch made_by_key) "q `touch made_by_tick`'
m[$k]='v $(touch made_by_value)'
declaration=$(declare -p m)
eval "${declaration/-A m=/-A copy=}"
for key in "${!m[@]}"; do
  [[ "${copy[$key]}" == "${m[$key]}" ]] || echo "declare lost key"
done
echo "copies=${#copy[@]} of ${#m[@]}"
listing="${m[@]@K}"
eval "pairs=($listing)"
echo "pairs=${#pairs[@]}"
declare -A one=([plain]=1)
echo "${one[@]@K}"
declare -A two=(["x y"]=2)
echo "${two[@]@K}"
declare -p one two
compgen -G 'made_by_*' || echo no-files
