#!/bin/bash
# A process id or signal number past the 32-bit range is an error, as in bash,
# instead of wrapping onto a small number. The wrapped values here would only
# probe the shell itself with signal 0, so a regression stays harmless.
kill -0 $(( $$ + 4294967296 )) 2> /dev/null
echo "wrapped-pid=$?"
kill -s 4294967296 $$ 2> /dev/null
echo "wrapped-signal=$?"
kill -l 4294967311 > /dev/null 2>&1
echo "wrapped-listing=$?"
kill -0 $$
echo "own-pid=$?"
