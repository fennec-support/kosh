# The sh mood keeps the obsolete $[ ] spelling literal, as dash does.
set -f
echo $[1+2] "$[ 2 * 3 ]" a$[x]b
x=$[1]
echo "$x"
cat <<EOF
here $[1+1]
EOF
echo "${unset_name:-$[7]}"
echo $(( $[3] + 1 ))
echo not reached
