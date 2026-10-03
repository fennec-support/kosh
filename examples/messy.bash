#!/bin/bash
deploy(){
local target=$1
if [ -z "$target" ];then
echo "usage: deploy TARGET">&2
    return 1
fi
for host in $(cat hosts.txt)
do
  if ssh "$host"   "test -d /srv/$target" ; then
echo "updating $host"
      rsync -az ./build/   "$host:/srv/$target/"
   else
 echo "skipping $host"
  fi
done
}
case "$1" in
deploy) deploy "$2";;
*) echo "unknown command: $1";;
esac
