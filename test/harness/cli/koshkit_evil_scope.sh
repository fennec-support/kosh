#!/bin/sh

default_report=$($BIN -c 'koshkit --color never evil')
case $default_report in
  *'  Users  '*) default_users=expanded ;;
  *) default_users=momentary ;;
esac
printf 'default-users=%s\n' "$default_users"
case $default_report in
  *Limits*|*Activity*|*Anomalies*) default_extended=extra ;;
  *) default_extended=omitted ;;
esac
printf 'default-extended=%s\n' "$default_extended"

users_report=$($BIN -c 'koshkit --color never evil --users')
case $users_report in
  *'  Users  '*) users_selector=matched ;;
  *) users_selector=missing ;;
esac
printf 'users-selector=%s\n' "$users_selector"

all_report=$($BIN -c 'koshkit --color never evil --all')
case $all_report in
  *'Mixed library ABIs'*'Finding confidence'*'Cost'*) all_extended=matched ;;
  *) all_extended=missing ;;
esac
if [ "$(uname -s)" = Linux ]; then
  case $all_report in
    *'Page faults  '*'Runnable processes  '*'Memory partial stall microseconds  '*'Finding confidence  '*'  high'*'Cost  '*'  process mappings'*) ;;
    *) all_extended=missing ;;
  esac
fi
printf 'all-extended=%s\n' "$all_extended"

short_all_report=$($BIN -c 'koshkit --color never evil -a')
case $short_all_report in
  *'Mixed library ABIs'*'Finding confidence'*) short_all=matched ;;
  *) short_all=missing ;;
esac
printf 'short-all=%s\n' "$short_all"

short_report=$($BIN -c 'koshkit --color never evil --short')
case $short_report in
  *Uptime*|*Resources*|*Limits*|*Activity*|*Anomalies*) short_scope=extra ;;
  *) short_scope=identity ;;
esac
printf 'short-scope=%s\n' "$short_scope"
