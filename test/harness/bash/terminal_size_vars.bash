#!/bin/bash

# A shell that is not interactive sets neither COLUMNS nor LINES. The probe
# runs a fresh shell with both removed from its environment, so a value the
# shell sets itself at startup shows in the output.
env -u COLUMNS -u LINES "$BASH" -c 'echo "COLUMNS=${COLUMNS-unset} LINES=${LINES-unset}"'
