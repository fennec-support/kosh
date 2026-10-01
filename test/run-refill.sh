#!/bin/bash
. ./utils.sh

TEST_SHELL_COMMAND=$1
REFILL_STATUS=0

# Refills one harness and keeps the first failing status. The named runner
# receives the refill flag and the remaining operands.
refill_harness()
{
  RUNNER_NAME=$1
  shift
  RUNNER_STATUS=0
  "$TEST_SHELL_COMMAND" "$RUNNER_NAME" --refill "$@" || RUNNER_STATUS=$?
  [ "$REFILL_STATUS" -ne 0 ] || REFILL_STATUS=$RUNNER_STATUS
}

if [ -n "${REFILL-}" ]; then
  SELECTED_NATIVE_TEST_NAMES=
  for TEST_NAME in $REFILL; do
    if word_is_listed "$TEST_NAME" "$ACTIVE_TEST_NAMES"; then
      SELECTED_NATIVE_TEST_NAMES="$SELECTED_NATIVE_TEST_NAMES $TEST_NAME"
    fi
  done
  if [ -n "$SELECTED_NATIVE_TEST_NAMES" ]; then
    refill_harness run-kosh-test.sh $SELECTED_NATIVE_TEST_NAMES
  fi

  for TEST_NAME in $REFILL; do
    DID_FIND_TEST=no
    if [ -f "kosh/$TEST_NAME.kosh" ]; then
      DID_FIND_TEST=yes
    fi

    if [ -f "cli/$TEST_NAME.sh" ]; then
      DID_FIND_TEST=yes
      if word_is_listed "cli/$TEST_NAME.sh" \
        "$PARALLEL_CLI_INPUT $SERIAL_CLI_INPUT"; then
        refill_harness run-cli-test.sh "$TEST_SHELL_COMMAND" \
          "cli/$TEST_NAME.sh"
      fi
    fi

    if [ -f "completion/$TEST_NAME.sh" ]; then
      DID_FIND_TEST=yes
      if word_is_listed "completion/$TEST_NAME.sh" \
        "$PARALLEL_COMPLETION_INPUT $SERIAL_COMPLETION_INPUT"; then
        refill_harness run-editor-test.sh completion "$TEST_SHELL_COMMAND" \
          "completion/$TEST_NAME.sh"
      fi
    fi

    if [ -f "highlight/$TEST_NAME.sh" ]; then
      DID_FIND_TEST=yes
      refill_harness run-editor-test.sh highlight "$TEST_SHELL_COMMAND" \
        "highlight/$TEST_NAME.sh"
    fi

    if [ "$DID_FIND_TEST" = no ]; then
      printf "Unknown refill test: %s\n" "$TEST_NAME" >&2
      [ "$REFILL_STATUS" -ne 0 ] || REFILL_STATUS=1
    fi
  done

  exit "$REFILL_STATUS"
fi

refill_harness run-kosh-test.sh $ACTIVE_TEST_NAMES
refill_harness run-cli-test.sh "$TEST_SHELL_COMMAND" \
  $PARALLEL_CLI_INPUT $SERIAL_CLI_INPUT
refill_harness run-editor-test.sh completion "$TEST_SHELL_COMMAND" \
  $PARALLEL_COMPLETION_INPUT $SERIAL_COMPLETION_INPUT
refill_harness run-editor-test.sh highlight "$TEST_SHELL_COMMAND" \
  $HIGHLIGHT_INPUT

exit "$REFILL_STATUS"
