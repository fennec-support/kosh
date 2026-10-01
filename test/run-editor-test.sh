#!/bin/bash
. ./runner-status.sh

REFILL_MODE=no
TEST_STATUS=0
if [ "${1-}" = --refill ]; then
  REFILL_MODE=yes
  shift
fi

TEST_GROUP=$1
TEST_SHELL_COMMAND=$2
shift 2

case $TEST_GROUP in
  completion|highlight) ;;
  *) printf 'invalid editor test group: %s\n' "$TEST_GROUP" >&2; exit 125 ;;
esac

if [ "${IS_NONDEBUG_BUILD:-0}" = 1 ]; then
  printf "\t%-64s skipped, release binary\n" "$TEST_GROUP"
  exit 0
fi

for TEST_FILE in "$@"; do
  TEST_NAME=${TEST_FILE##*/}
  TEST_NAME=${TEST_NAME%.sh}
  if [ "$REFILL_MODE" = yes ]; then
    OUTPUT="expected/.$TEST_NAME.out.tmp"
  else
    OUTPUT_DIRECTORY="$TEST_TEMP_DIRECTORY/results/$TEST_GROUP"
    "$TEST_KOSHKIT" mkdir -p "$OUTPUT_DIRECTORY"
    OUTPUT="$OUTPUT_DIRECTORY/$TEST_NAME.out"
  fi

  BIN="$BIN" run_test_with_timeout "${EDITOR_TEST_TIMEOUT_SECONDS:-60}" \
    "$TEST_SHELL_COMMAND" "$TEST_FILE" > "$OUTPUT" 2>/dev/null
  DRIVER_STATUS=$?
  if is_driver_status_harness_failure "$DRIVER_STATUS" "$REFILL_MODE"; then
    printf "\t%-64s harness failure, status %s\n" \
      "$TEST_GROUP/$TEST_NAME.sh" "$DRIVER_STATUS"
    "$TEST_KOSHKIT" rm -f "$OUTPUT"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=$DRIVER_STATUS
    fi
    continue
  fi

  if [ "$REFILL_MODE" = yes ]; then
    "$TEST_KOSHKIT" mv "$OUTPUT" "expected/$TEST_NAME.out"
    printf "\t%-64s %s.out\n" "$TEST_GROUP/$TEST_NAME.sh" "$TEST_NAME"
    continue
  fi

  if diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" >/dev/null 2>&1; then
    printf "\t%-64s ok\033[K\r" "$TEST_GROUP/$TEST_NAME.sh"
  else
    set_golden_failure_file "$TEST_GROUP-$TEST_NAME"
    diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" | \
      "$TEST_KOSHKIT" tee -a "$GOLDEN_FAILURE_FILE"
    printf "\t%-64s FAILED :c\n" "$TEST_GROUP/$TEST_NAME.sh"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=1
    fi
  fi
  "$TEST_KOSHKIT" rm -f "$OUTPUT"
done

exit "$TEST_STATUS"
