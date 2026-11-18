#!/bin/bash
# an external command at the end of a trap action leaves the shell alive
# to execute the command after the triggering kill.

trap '/bin/echo in-usr1-action' USR1
kill -USR1 $$
echo after-usr1
