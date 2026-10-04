#!/bin/sh

report=$($BIN -c 'koshkit --color never evil -a')
case $report in
  *'Mixed library ABIs'*'Finding confidence'*'Cost'*)
    anomaly_shape=matched
    ;;
  *) anomaly_shape=wrong ;;
esac
if [ "$(uname -s)" = Linux ]; then
  case $report in
    *'Page faults  '*'Runnable processes  '*'Memory partial stall microseconds  '*'Finding confidence  '*'  high'*'Cost  '*'  process mappings'*) ;;
    *) anomaly_shape=wrong ;;
  esac
fi
printf 'anomaly-shape=%s\n' "$anomaly_shape"

default_report=$($BIN -c 'koshkit --color never evil')
case $default_report in
  *Anomalies*) default_shape=extra ;;
  *) default_shape=portable ;;
esac
printf 'default-shape=%s\n' "$default_shape"
