unset KOSH_FLAGS
"$BIN" -c 'echo one' -c 'echo two'
"$BIN" --command='echo equals-form'
"$BIN" -N -c 'echo trailer'
echo "rc=$?"
"$BIN" --no-init-files -c ''
echo "empty-command-rc=$?"

success_output=$("$BIN" --no-diagnostics --show-exit-code -c ':' 2>&1)
[ -z "$success_output" ] || exit 1
echo "show-exit-code success is quiet"

"$BIN" --help 2>&1 | grep -F 'Show diagnostics for every non-zero exit' >/dev/null || exit 1
"$BIN" --help 2>&1 | grep -F 'Show diagnostics for every exit code,' >/dev/null || exit 1
"$BIN" -c 'set --help' 2>&1 | grep -F 'Show diagnostics for every exit code,' >/dev/null || exit 1
echo "show-exit-code help describes diagnostics"

continued_output=$("$BIN" --no-diagnostics --show-exit-code -c 'false; echo after' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$continued_output" | grep -c 'warning: Non-zero exit code (1)')" -eq 1 ] || exit 1
printf '%s\n' "$continued_output" | grep -Eq '^-c:[0-9]+:[0-9]+: warning: Non-zero exit code \(1\)$' || exit 1
echo "show-exit-code reports a located continued failure"

errexit_output=$("$BIN" --no-diagnostics --show-exit-code -e -c 'false; echo never' 2>&1)
errexit_status=$?
[ "$errexit_status" -eq 1 ] || exit 1
[ "$(printf '%s\n' "$errexit_output" | grep -c 'error: Non-zero exit code (1)')" -eq 1 ] || exit 1
printf '%s\n' "$errexit_output" | grep -Eq '^-c:[0-9]+:[0-9]+: error: Non-zero exit code \(1\)$' || exit 1
echo "show-exit-code reports before errexit"

guarded_output=$("$BIN" --no-diagnostics --show-exit-code -e -c 'false || true; ! false; if false; then :; fi; echo after' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$guarded_output" | grep -c 'warning: Non-zero exit code (1)')" -eq 2 ] || exit 1
echo "show-exit-code reports guarded failures once"

pipeline_output=$("$BIN" --no-diagnostics --show-exit-code -c 'set -M bash; set +o pipefail; false | true; echo after' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$pipeline_output" | grep -c 'Non-zero exit code')" -eq 0 ] || exit 1
pipefail_output=$("$BIN" --no-diagnostics --show-exit-code -c 'set -o pipefail; false | true; echo after' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$pipefail_output" | grep -c 'warning: Non-zero exit code (1)')" -eq 1 ] || exit 1
echo "show-exit-code follows the effective pipeline status"

pipefail_errexit_output=$("$BIN" --no-diagnostics --show-exit-code -e -c 'set -o pipefail; false | true; echo never' 2>&1)
[ "$?" -eq 1 ] || exit 1
[ "$(printf '%s\n' "$pipefail_errexit_output" | grep -c 'error: Non-zero exit code (1)')" -eq 1 ] || exit 1
echo "show-exit-code reports fatal pipefail as an error"

runtime_option_output=$("$BIN" --no-diagnostics -c 'koshconf set kosh.debug.report_nonzero_exit_codes on; "$1" --no-diagnostics -c "exit 7"' kosh-test "$BIN" 2>&1)
[ "$?" -eq 7 ] || exit 1
[ "$(printf '%s\n' "$runtime_option_output" | grep -c 'warning: Non-zero exit code (7)')" -eq 1 ] || exit 1
echo "runtime show-exit-code survives the terminal command"

runtime_errexit_output=$(
  "$BIN" --no-diagnostics -c 'koshconf set kosh.debug.report_nonzero_exit_codes on; set -e; false' 2>&1
)
runtime_errexit_status=$?
[ "$runtime_errexit_status" -eq 1 ] || exit 1
[ "$(printf '%s\n' "$runtime_errexit_output" | grep -c 'error: Non-zero exit code (1)')" -eq 1 ] || exit 1
echo "runtime show-exit-code reports errexit as an error"

all_codes_output=$("$BIN" --no-diagnostics -N -c 'true; false; echo after' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$all_codes_output" | grep -c 'warning: Exit code (0)')" -eq 2 ] || exit 1
[ "$(printf '%s\n' "$all_codes_output" | grep -c 'warning: Non-zero exit code (1)')" -eq 1 ] || exit 1
echo "show-all-exit-codes reports a successful zero"

clustered_all_codes_output=$("$BIN" --no-diagnostics -Ne -c 'true; echo after' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$clustered_all_codes_output" | grep -c 'warning: Exit code (0)')" -eq 2 ] || exit 1
echo "the clustered short form reports every exit code"

quiet_option_output=$("$BIN" --no-diagnostics -c 'koshconf set kosh.debug.report_nonzero_exit_codes on; true' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$quiet_option_output" | grep -c 'Exit code (0)')" -eq 0 ] || exit 1
echo "the non-zero level stays quiet on a success"

runtime_letter_output=$(
  "$BIN" --no-diagnostics -N -c 'false; set +N; false; set -N; false; echo after' 2>&1
)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$runtime_letter_output" | grep -c 'warning: Non-zero exit code (1)')" -eq 2 ] || exit 1
echo "set -N and set +N toggle the exit code diagnostic"

errtrace_letter_output=$("$BIN" --no-diagnostics -N -c 'set +E; false; echo after' 2>&1)
[ "$?" -eq 0 ] || exit 1
[ "$(printf '%s\n' "$errtrace_letter_output" | grep -c 'warning: Non-zero exit code (1)')" -eq 1 ] || exit 1
echo "set +E leaves the exit code diagnostic alone"

runtime_stats_output=$("$BIN" --no-diagnostics -c 'koshconf set kosh.debug.print_evaluation_statistics on; "$1" --no-diagnostics -c ":"' kosh-test "$BIN" 2>&1)
[ "$?" -eq 0 ] || exit 1
printf '%s\n' "$runtime_stats_output" | grep -F '[Stats' >/dev/null || exit 1
stats_breadth_output=$("$BIN" --no-diagnostics --show-stats -c 'f() { :; }; f; f' 2>&1)
for stats_label in 'Builtins run' 'Functions run' 'External commands run' 'Forks' 'Execs'; do
  printf '%s\n' "$stats_breadth_output" | grep -Eq "^  $stats_label: [0-9]+\$" || { echo "stats row missing: $stats_label"; exit 1; }
done
printf '%s\n' "$stats_breadth_output" | grep -Fxq '  Functions run: 2' || { echo "stats function count wrong"; exit 1; }
if [ "$(uname -s)" = Linux ]; then
  for stats_label in 'Bytes read' 'Bytes written' 'Read calls' 'Write calls' 'User time' 'System time' 'Peak RSS' 'Voluntary context switches' 'Involuntary context switches' 'Minor page faults' 'Major page faults'; do
    printf '%s\n' "$stats_breadth_output" | grep -Eq "^  $stats_label: " || { echo "stats row missing: $stats_label"; exit 1; }
  done
fi
echo "stats report counts, transfers, and resource usage"
runtime_memory_output=$("$BIN" --no-diagnostics -c 'koshconf set kosh.debug.print_memory_report_at_exit on; "$1" --no-diagnostics -c ":"' kosh-test "$BIN" 2>&1)
[ "$?" -eq 0 ] || exit 1
printf '%s\n' "$runtime_memory_output" | grep -F 'AST arena:' >/dev/null || exit 1
echo "runtime reports survive the terminal command"
