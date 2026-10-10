#!/bin/bash
# A redirection before an assignment-only command still applies, and an array
# assignment before a scalar one keeps both.
work_dir=$(mktemp -d)
cd "$work_dir" || exit 1

>./created cd_value=2
if [ -e created ]; then
  echo "created cd_value=$cd_value"
fi
>>./appended name=1 other=2
echo "appended=$([ -e appended ] && echo yes) name=$name other=$other"
list=(1 2) scalar=3
echo "list=${list[*]} scalar=$scalar"

cd / || exit 1
rm -r "$work_dir"
