#!/bin/bash
. ./utils.sh

if [ "${OS-}" != Windows_NT ]; then
  set -m
fi

SUITE_NAME=${1:-all}
WORKER_PIDS=
WORKER_COUNT=0
WORKER_STATUS=0

wait_for_workers()
{
  for WORKER_PID in $WORKER_PIDS; do
    WAIT_STATUS=0
    wait "$WORKER_PID" || WAIT_STATUS=$?
    [ "$WORKER_STATUS" -ne 0 ] || WORKER_STATUS=$WAIT_STATUS
  done
  WORKER_PIDS=
  WORKER_COUNT=0
}

wait_for_worker_slot()
{
  if [ "$WORKER_COUNT" -lt "$TEST_JOBS" ]; then
    return
  fi

  if [ "${OS-}" = Windows_NT ]; then
    set -- $WORKER_PIDS
    WORKER_PID=$1
    shift
    WAIT_STATUS=0
    wait "$WORKER_PID" || WAIT_STATUS=$?
    [ "$WORKER_STATUS" -ne 0 ] || WORKER_STATUS=$WAIT_STATUS
    WORKER_PIDS="$*"
    WORKER_COUNT=$((WORKER_COUNT - 1))
    return
  fi

  while :; do
    RUNNING_WORKER_PIDS=$(jobs -pr)
    for WORKER_PID in $WORKER_PIDS; do
      IS_RUNNING=no
      for RUNNING_WORKER_PID in $RUNNING_WORKER_PIDS; do
        if [ "$WORKER_PID" = "$RUNNING_WORKER_PID" ]; then
          IS_RUNNING=yes
          break
        fi
      done
      if [ "$IS_RUNNING" = yes ]; then
        continue
      fi

      WAIT_STATUS=0
      wait "$WORKER_PID" || WAIT_STATUS=$?
      [ "$WORKER_STATUS" -ne 0 ] || WORKER_STATUS=$WAIT_STATUS
      REMAINING_WORKER_PIDS=
      for REMAINING_WORKER_PID in $WORKER_PIDS; do
        if [ "$REMAINING_WORKER_PID" != "$WORKER_PID" ]; then
          REMAINING_WORKER_PIDS="$REMAINING_WORKER_PIDS $REMAINING_WORKER_PID"
        fi
      done
      WORKER_PIDS=$REMAINING_WORKER_PIDS
      WORKER_COUNT=$((WORKER_COUNT - 1))
      return
    done
    "$TEST_KOSHKIT" sleep 0.01
  done
}

run_harness_item()
{
  HARNESS_NAME=$1
  TEST_ITEM=$2

  case $HARNESS_NAME in
  kosh)
    "$TEST_SHELL" run-kosh-test.sh "$TEST_ITEM"
    ;;
  cli)
    "$TEST_SHELL" run-cli-test.sh "$TEST_SHELL" "$TEST_ITEM"
    ;;
  completion|highlight)
    "$TEST_SHELL" run-editor-test.sh "$HARNESS_NAME" "$TEST_SHELL" "$TEST_ITEM"
    ;;
  esac
}

run_parallel_harness()
{
  HARNESS_NAME=$1
  TEST_ITEMS=$2
  WORKER_STATUS=0

  for TEST_ITEM in $TEST_ITEMS; do
    run_harness_item "$HARNESS_NAME" "$TEST_ITEM" &
    WORKER_PIDS="$WORKER_PIDS $!"
    WORKER_COUNT=$((WORKER_COUNT + 1))
    wait_for_worker_slot
  done
  wait_for_workers

  return "$WORKER_STATUS"
}

print_platform_skips()
{
  HARNESS_NAME=$1

  case $HARNESS_NAME in
  kosh)
    for TEST_NAME in $SKIPPED_TESTS; do
      case "$TEST_NAME" in cli_*|completion_*) continue ;; esac
      printf "\t%-64s skipped, unsupported on current platform\n" \
        "$TEST_NAME.kosh"
    done
    ;;
  cli)
    for TEST_NAME in $SKIPPED_TESTS; do
      case "$TEST_NAME" in
      cli_*) printf "\t%-64s skipped, unsupported on current platform\n" \
        "cli/${TEST_NAME#cli_}.sh" ;;
      esac
    done
    ;;
  completion)
    for TEST_NAME in $SKIPPED_TESTS; do
      case "$TEST_NAME" in
      completion_*) printf "\t%-64s skipped, unsupported on current platform\n" \
        "completion/${TEST_NAME#completion_}.sh" ;;
      esac
    done
    ;;
  esac
}

run_serial_cli_tests()
{
  SERIAL_STATUS=0
  for TEST_FILE in $SERIAL_CLI_INPUT; do
    TEST_NAME=${TEST_FILE##*/}
    printf "\t%-64s running\n" "cli_${TEST_NAME%.sh}"
    RUNNER_STATUS=0
    run_harness_item cli "$TEST_FILE" || RUNNER_STATUS=$?
    [ "$SERIAL_STATUS" -ne 0 ] || SERIAL_STATUS=$RUNNER_STATUS
  done

  return "$SERIAL_STATUS"
}

run_serial_completion_tests()
{
  SERIAL_STATUS=0
  for TEST_FILE in $SERIAL_COMPLETION_INPUT; do
    TEST_NAME=${TEST_FILE##*/}
    printf "\t%-64s running\n" \
      "completion_${TEST_NAME%.sh}"
    RUNNER_STATUS=0
    run_harness_item completion "$TEST_FILE" || RUNNER_STATUS=$?
    [ "$SERIAL_STATUS" -ne 0 ] || SERIAL_STATUS=$RUNNER_STATUS
  done

  return "$SERIAL_STATUS"
}

run_named_suite()
{
  HARNESS_NAME=$1

  print_platform_skips "$HARNESS_NAME"
  case $HARNESS_NAME in
  kosh)
    run_parallel_harness kosh "$ACTIVE_TEST_NAMES"
    ;;
  cli)
    HARNESS_STATUS=0
    run_parallel_harness cli "$PARALLEL_CLI_INPUT" || HARNESS_STATUS=$?
    SERIAL_STATUS=0
    run_serial_cli_tests || SERIAL_STATUS=$?
    [ "$HARNESS_STATUS" -ne 0 ] || HARNESS_STATUS=$SERIAL_STATUS

    return "$HARNESS_STATUS"
    ;;
  completion)
    HARNESS_STATUS=0
    run_parallel_harness completion "$PARALLEL_COMPLETION_INPUT" || \
      HARNESS_STATUS=$?
    SERIAL_STATUS=0
    run_serial_completion_tests || SERIAL_STATUS=$?
    [ "$HARNESS_STATUS" -ne 0 ] || HARNESS_STATUS=$SERIAL_STATUS

    return "$HARNESS_STATUS"
    ;;
  highlight)
    run_parallel_harness highlight "$HIGHLIGHT_INPUT"
    ;;
  compat)
    if [ "${OS-}" = Windows_NT ]; then
      printf "\tcompat                                                        skipped, requires POSIX process semantics\n"
      return 0
    fi
    BIN="$BIN" BASHP="$BASHP" DASH="$DASH" \
      DIFF_FLAGS="$DIFF_FLAGS" FAILED_LIST="$FAILED_LIST" \
      SH_COMPAT_FILES="$SH_COMPAT_FILES" \
      BASH_COMPAT_FILES="$BASH_COMPAT_FILES" \
      "$TEST_SHELL" run-compat-diff-test.sh
    ;;
  esac
}

# Appends every recorded fixture diff to the shared failure list and prints the
# result. A golden runner writes one file per failing fixture. A concurrent
# worker cannot interleave its diff with another one. The glob orders the
# fixtures by name.
finish_results()
{
  "$TEST_KOSHKIT" touch "$FAILED_LIST"
  for RECORDED_DIFF in "$FAILED_LIST.d"/*.diff; do
    if [ -s "$RECORDED_DIFF" ]; then
      "$TEST_KOSHKIT" cat "$RECORDED_DIFF" >> "$FAILED_LIST"
    fi
  done
  "$TEST_KOSHKIT" rm -rf "$FAILED_LIST.d"

  "$TEST_KOSHKIT" cat "$FAILED_LIST"
  if [ -s "$FAILED_LIST" ]; then
    return 1
  fi

  return 0
}

if [ "$SUITE_NAME" != all ]; then
  "$TEST_KOSHKIT" rm -f "$FAILED_LIST"
  "$TEST_KOSHKIT" rm -rf "$FAILED_LIST.d"
  run_named_suite "$SUITE_NAME"
  SUITE_STATUS=$?
  RESULT_STATUS=0
  finish_results || RESULT_STATUS=$?
  [ "$SUITE_STATUS" -ne 0 ] || SUITE_STATUS=$RESULT_STATUS

  exit "$SUITE_STATUS"
fi

START_TIME=$("$TEST_KOSHKIT" date +%s)
SUITE_STATUS=0
"$TEST_KOSHKIT" rm -f "$FAILED_LIST" "$KOSH_HISTORY_FILE" "$KOSH_DIRECTORY_HISTORY"
"$TEST_KOSHKIT" rm -rf "$FAILED_LIST.d"
"$TEST_KOSHKIT" rm -rf "$PWD/.test-work"

RUNNER_STATUS=0
run_named_suite cli || RUNNER_STATUS=$?
[ "$SUITE_STATUS" -ne 0 ] || SUITE_STATUS=$RUNNER_STATUS

(
  PARALLEL_STATUS=0
  for HARNESS_NAME in kosh highlight completion; do
    RUNNER_STATUS=0
    run_named_suite "$HARNESS_NAME" || RUNNER_STATUS=$?
    [ "$PARALLEL_STATUS" -ne 0 ] || PARALLEL_STATUS=$RUNNER_STATUS
  done
  exit "$PARALLEL_STATUS"
) &
PARALLEL_PROCESS=$!

RUNNER_STATUS=0
run_named_suite compat || RUNNER_STATUS=$?
[ "$SUITE_STATUS" -ne 0 ] || SUITE_STATUS=$RUNNER_STATUS

RUNNER_STATUS=0
wait "$PARALLEL_PROCESS" || RUNNER_STATUS=$?
[ "$SUITE_STATUS" -ne 0 ] || SUITE_STATUS=$RUNNER_STATUS

RESULT_STATUS=0
finish_results || RESULT_STATUS=$?
[ "$SUITE_STATUS" -ne 0 ] || SUITE_STATUS=$RESULT_STATUS

ELAPSED_SECONDS=$(($("$TEST_KOSHKIT" date +%s) - START_TIME))
printf "\nDebug test step completed in %s seconds\n" "$ELAPSED_SECONDS"

exit "$SUITE_STATUS"
