#!/bin/bash
. ./runner-status.sh

REFILL_MODE=no
TEST_STATUS=0
if [ "${1-}" = --refill ]; then
  REFILL_MODE=yes
  shift
fi

TEST_SHELL_COMMAND=$1
shift

for TEST_FILE in "$@"; do
  TEST_NAME=${TEST_FILE##*/}
  TEST_NAME=${TEST_NAME%.sh}
  if [ "$REFILL_MODE" = yes ]; then
    OUTPUT="expected/.$TEST_NAME.out.tmp"
  else
    OUTPUT_DIRECTORY="$TEST_TEMP_DIRECTORY/results/cli"
    "$TEST_KOSHKIT" mkdir -p "$OUTPUT_DIRECTORY"
    OUTPUT="$OUTPUT_DIRECTORY/$TEST_NAME.out"
  fi

  GOLDEN_TIMEOUT_SECONDS=60
  if [ "$TEST_NAME" = history_behavior ] || [ "$TEST_NAME" = koshkit_timeout ]; then
    GOLDEN_TIMEOUT_SECONDS=120
  fi
  "$TEST_KOSHKIT" timeout -k 2s \
    "${CLI_TEST_TIMEOUT_SECONDS:-$GOLDEN_TIMEOUT_SECONDS}" \
    "$TEST_SHELL_COMMAND" "$TEST_FILE" > "$OUTPUT" 2>&1
  DRIVER_STATUS=$?

  if [ "$DRIVER_STATUS" -ne 0 ]; then
    "$TEST_KOSHKIT" cat "$OUTPUT"
    printf "\t%-64s harness failure, status %s\n" \
      "cli/$TEST_NAME.sh" "$DRIVER_STATUS"
    "$TEST_KOSHKIT" rm -f "$OUTPUT"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=$DRIVER_STATUS
    fi
    continue
  fi

  if [ "$REFILL_MODE" = yes ]; then
    "$TEST_KOSHKIT" mv "$OUTPUT" "expected/$TEST_NAME.out"
    printf "\t%-64s cli/%s.out\n" "cli/$TEST_NAME.sh" "$TEST_NAME"
    continue
  fi

  if diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" >/dev/null 2>&1; then
    printf "\t%-64s ok\033[K\r" "cli/$TEST_NAME.sh"
  else
    set_golden_failure_file "cli-$TEST_NAME"
    diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" | \
      "$TEST_KOSHKIT" tee -a "$GOLDEN_FAILURE_FILE"
    printf "\t%-64s FAILED :c (driver status %s)\n" \
      "cli/$TEST_NAME.sh" "$DRIVER_STATUS"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=1
    fi
  fi
  "$TEST_KOSHKIT" rm -f "$OUTPUT"
done

exit "$TEST_STATUS"
