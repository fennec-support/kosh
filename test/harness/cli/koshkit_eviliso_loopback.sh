#!/bin/sh

if test "$(uname -s)" != Linux; then
  echo 'platform=skipped'
  exit 0
fi

if ! command -v python3 > "$TEST_NULL_DEVICE" 2>&1; then
  echo 'python3=skipped'
  exit 0
fi

work=$TEST_MKTEMP_DIRECTORY/eviliso-loopback-$$
helper_pid=
has_release_descriptor=no

cleanup()
{
  if test -n "$helper_pid"; then
    kill "$helper_pid" 2> "$TEST_NULL_DEVICE"
    wait "$helper_pid" 2> "$TEST_NULL_DEVICE"
    helper_pid=
  fi
  if test "$has_release_descriptor" = yes; then
    exec 9>&-
    has_release_descriptor=no
  fi
  if test -n "$work" && test -d "$work"; then
    "$BIN" -c 'koshkit rm -rf -- "$1"' rm "$work"
  fi
}

port_name()
{
  case $1 in
  *:"$server_port") echo "${1%:*}:SERVER" ;;
  *:"$client_port") echo "${1%:*}:CLIENT" ;;
  *:"$server6_port") echo "${1%:*}:SERVER6" ;;
  *:"$client6_port") echo "${1%:*}:CLIENT6" ;;
  *) echo "$1" ;;
  esac
}

is_helper_row()
{
  case $6 in
  *:"$server_port"|*:"$client_port") return 0 ;;
  esac
  if test "$server6_port" != -; then
    case $6 in
    *:"$server6_port"|*:"$client6_port") return 0 ;;
    esac
  fi
  return 1
}

describe_detail_rows()
{
  report=$1
  expected_pid=$2
  expected_uid=$3
  expected_user=$4
  expected_net=$5
  style=$6
  rows=0
  while IFS= read -r row; do
    set -- $row
    is_helper_row "$@" || continue
    rows=$((rows + 1))
    if test "$#" -ne 19; then
      echo "detail-row: wrong field count $#"
      continue
    fi
    inode_state=other
    case $8 in
    "$server_inode"|"$client_inode"|"$server6_inode"|"$client6_inode")
      inode_state=helper
      ;;
    esac
    pid_state=wrong
    test "${9}" = "$expected_pid" && pid_state=helper
    uid_state=wrong
    test "${10}" = "$expected_uid" && uid_state=current
    user_state=wrong
    test "${11}" = "$expected_user" && user_state=current
    netns_state=wrong
    test "${15}" = "$expected_net" && netns_state=helper
    if test "$style" = raw; then
      context="orchestrator=${16} runtime=${17} container=${18}"
      context="$context cgroup=${19}"
    else
      context="context=present"
      for cell in "${16}" "${17}" "${18}" "${19}"; do
        test -n "$cell" || context="context=missing"
      done
    fi
    echo "detail-row: $1 $2 $3 $4 $5 $(port_name "$6") $(port_name "$7")" \
      "socket=$inode_state pid=$pid_state uid=$uid_state user=$user_state" \
      "name=${12} command=${13}_${14} netns=$netns_state $context"
  done < "$report"
  echo "detail-rows=$rows"
}

describe_summary_rows()
{
  rows=0
  while IFS= read -r row; do
    set -- $row
    is_helper_row "$@" || continue
    rows=$((rows + 1))
    echo "summary-row: $# fields: $1 $2 $3 $4 $5 $(port_name "$6") $(port_name "$7")"
  done < "$1"
  echo "summary-rows=$rows"
}

count_listener_rows()
{
  rows=0
  while IFS= read -r row; do
    set -- $row
    case $6 in
    *:"$server_port") test "$3" = LISTEN && rows=$((rows + 1)) ;;
    esac
  done < "$1"
  echo "listener-rows=$rows"
}

trap cleanup EXIT
mkdir -p "$work" || exit 1
ready=$work/ready
release=$work/release
mkfifo "$ready" "$release" || exit 1
exec 9<> "$release" || exit 1
has_release_descriptor=yes
ln -s "$(cd "$(dirname "$0")" && pwd)/koshkit_eviliso_loopback.py" \
  "$work/loop.py" || exit 1

(cd "$work" && exec python3 loop.py < "$release" > "$ready") &
helper_pid=$!
IFS=' ' read -r marker ready_pid server_port client_port server_inode \
  client_inode server6_port client6_port server6_inode client6_inode \
  < "$ready"

if test "$marker" = READY && test "$ready_pid" = "$helper_pid" && \
   test "$server_port" -gt 0 && test "$client_port" -gt 0 && \
   test "$server_port" != "$client_port"
then
  echo 'helper-ready=matched'
else
  echo 'helper-ready=wrong'
  exit 1
fi

host_net=$(readlink "/proc/$ready_pid/ns/net")
host_uid=$(id -u)
host_user=$(id -un)

summary_report=$work/summary
"$BIN" -c 'koshkit --color never eviliso --remote' > "$summary_report"
echo "summary-status=$?"
describe_summary_rows "$summary_report" | sort
case $(cat "$summary_report") in
*"Socket summary"*"Remote sockets"*"Total sockets"*"Remote peers"*)
  echo 'summary-sections=matched'
  ;;
*) echo 'summary-sections=wrong' ;;
esac

detail_report=$work/detail
"$BIN" -c 'koshkit --color never eviliso --remote --all' > "$detail_report"
echo "detail-status=$?"
describe_detail_rows "$detail_report" "$ready_pid" "$host_uid" "$host_user" \
  "$host_net" summary | sort
case $(cat "$detail_report") in
*"FAMILY"*"PROTO"*"STATE"*"RECV-Q"*"SEND-Q"*"LOCAL"*"PEER"*"SOCKET"*"PID"*\
"UID"*"USER"*"NAME"*"COMMAND"*"NETNS"*"ORCHESTRATOR"*"RUNTIME"*"CONTAINER"*\
"CGROUP"*) echo 'detail-header=matched' ;;
*) echo 'detail-header=wrong' ;;
esac
count_listener_rows "$detail_report"

printf x >&9
exec 9>&-
has_release_descriptor=no
wait "$helper_pid"
echo "helper-status=$?"
helper_pid=

after_report=$work/after
"$BIN" -c 'koshkit --color never eviliso --remote --all' > "$after_report"
owned_rows=0
while IFS= read -r row; do
  set -- $row
  is_helper_row "$@" || continue
  test "$#" -eq 19 && test "${9}" = "$ready_pid" && \
    owned_rows=$((owned_rows + 1))
done < "$after_report"
echo "owned-rows-after-exit=$owned_rows"

if ! unshare -Urnmpf --mount-proc true > "$TEST_NULL_DEVICE" 2>&1; then
  echo 'user-namespaces=skipped'
  exit 0
fi

hex=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
pod=12345678_1234_1234_1234_123456789abc
printf '0::/kubepods.slice/kubepods-burstable.slice/kubepods-burstable-pod%s.slice/cri-containerd-%s.scope\n' \
  "$pod" "$hex" > "$work/cgroup"
isolated=$work/isolated
unshare -Urnmpf --mount-proc sh -c '
  cd "$1" || exit 1
  rm -f ready release
  mkfifo ready release || exit 1
  exec 9<> release
  LOOPBACK_UP=1 python3 loop.py < release > ready &
  helper=$!
  IFS=" " read -r marker rest < ready || exit 1
  mount --bind cgroup "/proc/$helper/cgroup" || exit 1
  printf "%s\n%s %s\n%s %s\n" "$(readlink "/proc/$helper/ns/net")" \
    "$(id -u)" "$(id -un)" "$marker" "$rest"
  "$2" -c "koshkit --color never eviliso --remote --all"
  printf x >&9
  wait "$helper"
' sh "$work" "$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" \
  > "$isolated" 2>&1 \
  || echo 'isolated-launch=wrong'
{
  IFS= read -r isolated_net
  IFS=' ' read -r isolated_uid isolated_user
  IFS=' ' read -r marker isolated_pid server_port client_port server_inode \
    client_inode server6_port client6_port server6_inode client6_inode
} < "$isolated"
if test "$marker" = READY && test "$server_port" -gt 0; then
  echo 'isolated-ready=matched'
else
  echo 'isolated-ready=wrong'
  exit 1
fi
isolated_rows=$work/isolated-rows
"$BIN" -c 'koshkit sed -n "4,\$p" "$1"' sed "$isolated" > "$isolated_rows"
describe_detail_rows "$isolated_rows" "$isolated_pid" "$isolated_uid" \
  "$isolated_user" "$isolated_net" raw | sort
count_listener_rows "$isolated_rows"
