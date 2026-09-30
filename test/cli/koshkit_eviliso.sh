#!/bin/sh

run_report()
{
  "$BIN" -c "koshkit --color never eviliso $1"
}

has_cgroup_section()
{
  case $1 in
  *"Cgroup membership"*|*"Cgroup status failures"*|*"Membership unavailable"*)
    return 0
    ;;
  *) return 1 ;;
  esac
}

has_namespace_section()
{
  printf '%s\n' "$1" | command grep -E \
    '^[[:space:]]+TYPE +ID +(PROCESSES|PID +NAME +ROLE) *$' \
    > "$TEST_NULL_DEVICE" 2>&1
}

has_session_section()
{
  printf '%s\n' "$1" | command grep -E \
    '^[[:space:]]+USER +TERMINAL( +LOGIN TIME)? *$' \
    > "$TEST_NULL_DEVICE" 2>&1
}

default_report=$(run_report "")
default_shape=matched
case $default_report in
  *"Socket summary"*"Container runtimes"*)
    default_shape=matched
    ;;
  *) default_shape=missing ;;
esac
has_namespace_section "$default_report" || default_shape=missing
has_cgroup_section "$default_report" || default_shape=missing
has_session_section "$default_report" || default_shape=missing
printf 'default-shape=%s\n' "$default_shape"

cgroup_detail_report=$(run_report '-a -c')
detail_shape=matched
case $cgroup_detail_report in
  *"Cgroup membership"*"HIERARCHY"*"CONTROLLER"*"PATH"*"PID"*"NAME"*"ROLE"*"self"*) ;;
  *"Cgroup status failures"*"Unavailable"*) ;;
  *"Cgroup status failures"*"unavailable"*) ;;
  *"Cgroup status failures"*"empty"*) ;;
  *"Membership unavailable"*) ;;
  *) detail_shape=missing ;;
esac
printf 'detail-shape=%s\n' "$detail_shape"

remote_report=$(run_report '--remote --all')
case $remote_report in
  *"Socket summary"*"Remote peers"*"FAMILY"*"PROTO"*"STATE"*"RECV-Q"*\
*"SEND-Q"*"LOCAL"*"PEER"*"SOCKET"*"PID"*"UID"*"USER"*"NAME"*\
*"COMMAND"*"NETNS"*"ORCHESTRATOR"*"RUNTIME"*"CONTAINER"*"CGROUP"*)
    remote_table=matched
    ;;
  *) remote_table=missing ;;
esac
printf 'remote-table=%s\n' "$remote_table"

for selector_section in \
  'namespaces|-n|namespace-section' \
  'cgroups|-c|cgroup-section' \
  'sessions|-s|session-section' \
  'remote|-r|Socket summary' \
  'runtime|-k|Container runtimes'; do
  old_ifs=$IFS
  IFS='|'
  set -- $selector_section
  IFS=$old_ifs
  report=$(run_report "$2")
  if test "$1" = namespaces; then
    if has_namespace_section "$report"; then
      selector_status=matched
    else
      selector_status=missing
    fi
  elif test "$1" = cgroups; then
    if has_cgroup_section "$report"; then
      selector_status=matched
    else
      selector_status=missing
    fi
  elif test "$1" = sessions; then
    if has_session_section "$report"; then
      selector_status=matched
    else
      selector_status=missing
    fi
  else
    case $report in
      *"$3"*) selector_status=matched ;;
      *) selector_status=missing ;;
    esac
  fi
  printf '%s-selector=%s\n' "$1" "$selector_status"

  selector_scope=matched
  case $1 in
  namespaces)
    case $report in
    *"Cgroup membership"*|*"Socket summary"*|*"Container runtimes"*)
      selector_scope=wrong
      ;;
    esac
    ;;
  cgroups)
    case $report in
    *"Socket summary"*|*"Container runtimes"*)
      selector_scope=wrong
      ;;
    esac
    ;;
  sessions)
    case $report in
    *"Namespaces"*|*"Socket summary"*|*"Container runtimes"*)
      selector_scope=wrong
      ;;
    esac
    ;;
  remote)
    case $report in
    *"Cgroup membership"*|*"Container runtimes"*)
      selector_scope=wrong
      ;;
    esac
    ;;
  runtime)
    case $report in
    *"Namespaces"*|*"Socket summary"*)
      selector_scope=wrong
      ;;
    esac
    ;;
  esac
  if test "$1" != namespaces && has_namespace_section "$report"; then
    selector_scope=wrong
  fi
  if test "$1" != sessions && has_session_section "$report"; then
    selector_scope=wrong
  fi
  printf '%s-scope=%s\n' "$1" "$selector_scope"
done

containers_report=$(run_report '--containers')
containers_alias=missing
case $containers_report in
  *"Containers"*) containers_alias=matched ;;
esac
case $containers_report in
  *"Cgroup membership"*|*"Socket summary"*|*"USER   TERMINAL"*)
    containers_alias=wrong
    ;;
esac
printf 'containers-alias=%s\n' "$containers_alias"

combined_report=$(run_report '-n -k')
case $combined_report in
  *"Container runtimes"*) combined_scope=matched ;;
*) combined_scope=missing ;;
esac
has_namespace_section "$combined_report" || combined_scope=missing
case $combined_report in
  *"Cgroup membership"*|*"Socket summary"*) combined_scope=wrong ;;
esac
if has_session_section "$combined_report"; then combined_scope=wrong; fi
printf 'combined-scope=%s\n' "$combined_scope"

namespace_detail=$(run_report '-a -n')
if has_namespace_section "$namespace_detail"; then
  all_scope=matched
else
  all_scope=missing
fi
case $namespace_detail in
  *"Cgroup membership"*|*"Socket summary"*|*"Container runtimes"*)
    all_scope=wrong
    ;;
esac
if has_session_section "$namespace_detail"; then all_scope=wrong; fi
printf 'all-scope=%s\n' "$all_scope"

all_report=$(run_report -a)
case $all_report in
  *"Namespaces"*"Socket summary"*"Container runtimes"*)
  all_default_scope=matched
  ;;
  *"Membership unavailable"*"Socket summary"*"Container runtimes"*)
  all_default_scope=matched
  ;;
*) all_default_scope=missing ;;
esac
has_namespace_section "$all_report" || all_default_scope=missing
has_session_section "$all_report" || all_default_scope=missing
printf 'all-default-scope=%s\n' "$all_default_scope"

help=$($BIN -c 'koshkit eviliso --help')
case $help in
  *"--all"*"--namespaces"*"--cgroups"*"--sessions"*"--remote"*"--runtime"*\
*"--kubernetes"*"--containers"*)
    help_shape=matched
    ;;
  *) help_shape=wrong ;;
esac
case $help in
*"--detail"*) help_shape=wrong ;;
esac
printf 'help-shape=%s\n' "$help_shape"

"$BIN" -c 'koshkit evilps >/dev/null; koshkit evilss -x >/dev/null; koshkit eviliso -n >/dev/null'
echo "unprivileged-status=$?"
