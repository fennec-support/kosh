#!/bin/bash
# jobs reports a job that a signal ended by the signal description, a job that
# exited by Done or by Exit and its status, and a running job with a trailing
# ampersand, all in the columns bash uses. The long form adds the process id.
# A wait for a job that a signal ended still reports 128 plus the signal. Only
# SIGTERM ends the jobs here, since bash reports most other signals on its
# error stream whenever it reaps the job before jobs runs. The settled listings
# leave out the current-job marker, which Bash gives a finished job or not
# depending on when it reaped the job.
listing=$(mktemp)
settle() {
  for _ in {1..500}; do
    jobs "$@" >"$listing"
    [[ $(<"$listing") == *Running* ]] || break
    sleep 0.01
  done
  sed -e 's/^\(\[[0-9]*\]\)[+-]/\1 /' "$listing"
}

sleep 30 & kill %1
settle
sh -c 'exit 3' &
settle
true &
settle

sleep 30 &
sleep 31 | sleep 32 &
jobs
jobs -r
kill %2
settle %2
kill %1
settle -l >/dev/null
sed 's/^\(\[[0-9]*\][-+ ]\)  *[0-9][0-9]* /\1 PID /' "$listing"

sleep 30 & pid=$!
kill "$pid"
wait "$pid"
echo "wait=$?"
rm -f "$listing"
