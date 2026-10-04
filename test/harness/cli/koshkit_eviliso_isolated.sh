#!/bin/sh

if test "$(uname -s)" != Linux; then
  echo 'platform=skipped'
  exit 0
fi

if ! unshare -Urmpnf --mount-proc true > "$TEST_NULL_DEVICE" 2>&1; then
  echo 'user-namespaces=skipped'
  exit 0
fi

work=$TEST_MKTEMP_DIRECTORY/eviliso-isolated-$$

cleanup()
{
  if test -n "$work" && test -d "$work"; then
    "$BIN" -c 'koshkit rm -rf -- "$1"' rm "$work"
  fi
}

trap cleanup EXIT
mkdir -p "$work" || exit 1
ln -s "$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")" \
  "$work/isolated-kosh" || exit 1

run_namespaces()
{
  "$@" "$BIN" -c 'koshkit --color never eviliso -n -a'
}

self_namespace_ids()
{
  while read -r type id pid name role; do
    test "$role" = self && printf '%s=%s\n' "$type" "$id"
  done
}

host_ids=$work/host-ids
run_namespaces env | self_namespace_ids > "$host_ids"
host_net=$(readlink /proc/self/ns/net)
host_user=$(readlink /proc/self/ns/user)
if test "net:[$(sed -n 's/^net=//p' "$host_ids")]" = "$host_net" && \
   test "user:[$(sed -n 's/^user=//p' "$host_ids")]" = "$host_user"
then
  echo 'host-self-namespaces=matched'
else
  echo 'host-self-namespaces=wrong'
fi

compare_namespaces()
{
  label=$1
  shift
  child_ids=$work/child-ids
  run_namespaces "$@" | self_namespace_ids > "$child_ids"
  differing=
  while IFS== read -r type id; do
    host_id=$(sed -n "s/^$type=//p" "$host_ids")
    if test "$id" != "$host_id"; then
      differing="$differing${differing:+,}$type"
    fi
  done < "$child_ids"
  echo "$label differs: ${differing:-none}"
}

compare_namespaces 'unshare -Ur' unshare -Ur
compare_namespaces 'unshare -Urm' unshare -Urm
compare_namespaces 'unshare -Urn' unshare -Urn
compare_namespaces 'unshare -Uri' unshare -Uri
compare_namespaces 'unshare -Uru' unshare -Uru
compare_namespaces 'unshare -Urc' unshare -Urc
compare_namespaces 'unshare -Urpf --mount-proc' unshare -Urpf --mount-proc

run_isolated()
{
  cgroup_file=$1
  options=$2
  shift 2
  unshare -Urnmpf --mount-proc env "$@" sh -c '
    mount --bind "$1" "/proc/$$/cgroup" || exit 1
    exec "$2" -c "$3"
  ' sh "$cgroup_file" "$work/isolated-kosh" \
    "koshkit --color never eviliso $options" 2>&1
}

show_case()
{
  title=$1
  options=$2
  content=$3
  shift 3
  cgroup_file=$work/cgroup
  printf '%s' "$content" > "$cgroup_file"
  echo "== $title: $options"
  run_isolated "$cgroup_file" "$options" "$@"
  echo "status=$?"
}

hex_a=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
hex_b=fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210
uid_a=12345678-1234-1234-1234-123456789abc
uid_b=ABCDEF01_2345_6789_ABCD_EF0123456789

show_case 'empty cgroup file' '-c -k --containers --kubernetes' ''

show_case 'systemd burstable containerd' \
  '-c -k --containers --kubernetes' \
  "0::/kubepods.slice/kubepods-burstable.slice/kubepods-burstable-pod$(
    printf '%s' "$uid_a" | tr - _
  ).slice/cri-containerd-$hex_a.scope
"

show_case 'systemd guaranteed cri-o' '-k --containers --kubernetes' \
  "0::/kubepods.slice/kubepods-pod$(
    printf '%s' "$uid_a" | tr - _
  ).slice/crio-$hex_b.scope
"

show_case 'systemd besteffort uppercase pod' '--kubernetes' \
  "0::/kubepods.slice/kubepods-besteffort.slice/kubepods-besteffort-pod$uid_b.slice/docker-$hex_a.scope
"

show_case 'cgroupfs besteffort' '-k --containers --kubernetes' \
  "0::/kubepods/besteffort/pod$uid_a/$hex_a
"

show_case 'cgroupfs burstable detail' '-a -k --containers --kubernetes' \
  "0::/kubepods/burstable/pod$uid_a/$hex_b
"

show_case 'cgroupfs guaranteed detail' '-a --kubernetes' \
  "0::/kubepods/pod$uid_a/$hex_a
"

show_case 'docker v1 hierarchies and duplicates' '-c -k --containers' \
  "12:pids:/docker/$hex_a
11:memory:/docker/$hex_a
11:memory:/docker/$hex_a
1:name=systemd:/docker/$hex_a
0::/docker/$hex_a
"

show_case 'docker v1 detail' '-a -c -k --containers' \
  "12:pids:/docker/$hex_a
11:memory:/docker/$hex_a
11:memory:/docker/$hex_a
0::/docker/$hex_a
"

show_case 'podman and libpod' '-k --containers' \
  "1:name=systemd:/machine.slice/libpod-$hex_a.scope
0::/user.slice/libpod/$hex_b
"

show_case 'malformed lines are skipped' '-c -k --containers' \
  "no separators
one:colon
x:memory:/docker/$hex_a
:memory:/docker/$hex_a
3:memory:
-1:memory:/docker/$hex_a
99999999999999999999:memory:/docker/$hex_a
2:cpu:/valid/path
"

show_case 'invalid identifiers are rejected' '-c -k --containers --kubernetes' \
  "6:cpu:/docker/$(printf '%s' "$hex_a" | cut -c1-63)
5:cpu:/docker/${hex_a%?}g
4:cpu:/kubepods/besteffort/pod12345678-1234-1234-1234-12345678XXXX/$hex_b
3:cpu:/kubepods/besteffort/pod12345678-1234-1234-1234-123456789abcd
2:cpu:/system.slice/docker-$(printf '%s' "$hex_a" | cut -c1-63).scope
"

show_case 'kubernetes environment' '--kubernetes' \
  "0::/user.slice
" KUBERNETES_SERVICE_HOST=10.96.0.1

show_case 'kubernetes environment control bytes' '--kubernetes' \
  "0::/user.slice
" "KUBERNETES_SERVICE_HOST=api	host"

long_host=$(printf '%0300d' 0 | tr 0 h)
host_report=$(show_case 'kubernetes environment truncation' '--kubernetes' \
  "0::/user.slice
" "KUBERNETES_SERVICE_HOST=$long_host")
longest=0
for word in $host_report; do
  case $word in
  h*) test "${#word}" -gt "$longest" && longest=${#word} ;;
  esac
done
echo "kubernetes-host-bytes=$longest"

other_report=$(unshare -Urnmpf --mount-proc sh -c '
  printf "0::/docker/%s\n" "$2" > "$1/self"
  printf "0::/kubepods/besteffort/pod%s/%s\n" "$3" "$4" > "$1/other"
  sleep 30 &
  other=$!
  sleep 30 &
  sibling=$!
  for pid in $other $sibling; do
    count=0
    comm=
    while test "$comm" != sleep && test "$count" -lt 200000; do
      read -r comm < "/proc/$pid/comm"
      count=$((count + 1))
    done
  done
  mount --bind "$1/other" "/proc/$other/cgroup" || exit 1
  mount --bind "$1/self" "/proc/$sibling/cgroup" || exit 1
  mount --bind "$1/self" "/proc/$$/cgroup" || exit 1
  exec "$5" -c "koshkit --color never eviliso -a -c -k --containers --kubernetes"
' sh "$work" "$hex_a" "$uid_a" "$hex_b" "$work/isolated-kosh" 2>&1)
printf '== self and other process\n%s\n' "$other_report"

service_account_report()
{
  unshare -Urnmpf --mount-proc env KUBERNETES_SERVICE_HOST=10.96.0.1 sh -c '
    mount -t tmpfs tmpfs /var/run || exit 1
    mkdir -p /var/run/secrets/kubernetes.io/serviceaccount || exit 1
    printf "%b" "$1" > /var/run/secrets/kubernetes.io/serviceaccount/namespace
    printf "0::/user.slice\n" > "$2/cgroup"
    mount --bind "$2/cgroup" "/proc/$$/cgroup" || exit 1
    exec "$3" -c "koshkit --color never eviliso --kubernetes"
  ' sh "$1" "$work" "$work/isolated-kosh" 2>&1
}

echo '== service account namespace file'
service_account_report 'team-a\nsecond line\n'
echo "status=$?"
echo '== service account namespace file with control bytes'
service_account_report 'team\tb\033[0m\n'
echo "status=$?"
echo '== service account empty namespace file'
service_account_report ''
echo "status=$?"
