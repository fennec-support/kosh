unset KOSH_FLAGS
# The KOSH_ANSI_ color variables expose the SGR escapes a prompt template uses.
# the harness redirects stdout to a file, and colors::stdout_wants_color()
# is false. ${!ref-MISSING} distinguishes recognized empty values from
# unknown names without requiring a terminal.


names="BLACK RED GREEN YELLOW BLUE MAGENTA CYAN WHITE \
BRIGHT_BLACK BRIGHT_RED BRIGHT_GREEN BRIGHT_YELLOW \
BRIGHT_BLUE BRIGHT_MAGENTA BRIGHT_CYAN BRIGHT_WHITE BOLD DIM RESET BOGUS"
NAMES=$names "$BIN" -c 'for name in $NAMES; do
  ref=KOSH_ANSI_$name
  printf "%-16s [%s]\n" "$ref" "${!ref-MISSING}"
done'
# A stored assignment wins over the computed escape.
"$BIN" -c 'KOSH_ANSI_RED=stored; printf "stored=[%s]\n" "$KOSH_ANSI_RED"'
# The recognition holds in the sh mood the same as the default mood.
"$BIN" --mood sh -c 'printf "sh_green=[%s]\n" "${KOSH_ANSI_GREEN-MISSING}"'
