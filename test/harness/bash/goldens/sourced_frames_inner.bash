frames()
{
  local names source_names
  source_names=
  for name in "${BASH_SOURCE[@]}"; do
    source_names="$source_names ${name##*/}"
  done
  echo "$1 line=$LINENO func=${FUNCNAME[*]} source=$source_names"
  echo "$1 bash_lineno=${BASH_LINENO[*]}"
}

inner_function()
{
  echo "inner start $LINENO"
  frames inner
  nested_function
}

nested_function()
{
  frames nested
}

echo "inner file top line $LINENO"
frames inner_top
