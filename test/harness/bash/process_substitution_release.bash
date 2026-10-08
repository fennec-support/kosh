#!/bin/bash
# shellcheck disable=SC2194,SC2206
# A process substitution in a [[ ]] operand, a case word or pattern, or the
# operand word of a parameter expansion in an assignment value closes its
# descriptor and reaps its child when that command finishes, at top level and
# in while and until loops, checked against bash through /proc on Linux. A
# process substitution in a pipeline stage is released once the pipeline is
# reaped, so its output comes first and waiting for it returns, under a
# watchdog that ends a shell that hangs.
count_fds() {
  local entries=(/proc/$$/fd/*)
  fd_count=${#entries[@]}
}
count_zombies() {
  local child_ids child_id stat_line
  zombie_count=0
  read -r -a child_ids <"/proc/$$/task/$$/children"
  for child_id in "${child_ids[@]}"; do
    { read -r stat_line <"/proc/$child_id/stat"; } 2>/dev/null || continue
    read -r -a stat_line <<<"${stat_line##*) }"
    if [[ ${stat_line[0]} == Z ]]; then
      zombie_count=$((zombie_count + 1))
    fi
  done
}
report() {
  count_fds
  count_zombies
  echo "$1: fds+$((fd_count - base_fd_count)) zombies=$zombie_count"
}
unset u w
count_fds
base_fd_count=$fd_count

[[ -e <(echo d) ]]
report conditional
case <(echo d) in *) ;; esac
report case-word
case y in <(echo d)) ;; esac
report case-pattern
v=${u:-<(echo d)}
report default
v=${u-${w:-<(echo d)}}
report nested-default
v=$(true)${u:-<(echo d)}
report after-command-substitution
v+=${u:-<(echo d)}
report append
a[1]=${u:-<(echo d)}
report array-element

i=0
while ((i++ < 20)); do
  [[ -e <(echo d) ]]
  case <(echo d) in *) ;; esac
  case y in <(echo d)) ;; esac
  v=${u:-<(echo d)}
  v=${u-${w:-<(echo d)}}
  v=$(true)${u:-<(echo d)}
  v+=${u:-<(echo d)}
  a[1]=${u:-<(echo d)}
done
report while-loop

i=0
until ((i++ >= 20)); do
  [[ -e <(echo d) ]]
  case <(echo d) in *) ;; esac
  v=${u:-<(echo d)}
  a[1]=${u:-<(echo d)}
done
report until-loop

[[ $(cat <(echo readable)) == readable ]] && echo "conditional-readable"
case $(cat <(echo word)) in word) echo "case-readable" ;; esac

watchdog_file=$(mktemp)
(
  {
    sleep 30
    kill -KILL "$$"
  } >/dev/null 2>&1 &
  echo "$!" >"$watchdog_file"
)
read -r watchdog_pid <"$watchdog_file"
rm -f "$watchdog_file"
echo x | tee >(wc -c) >/dev/null
sleep 1
echo "after-tee"
true | true >(cat)
wait
echo "output-substitution-waited=$?"
cat <(yes) | head -n1
wait
echo "endless-substitution-waited"
report pipeline-stage
kill "$watchdog_pid"
