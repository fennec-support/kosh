#!/bin/bash
. ./runner-status.sh

if [ "${1-}" = --windows-driver ]; then
  shift
  set -m
  sh "$1" &
  GOLDEN_PROCESS=$!
  set +m
  trap 'taskkill.exe //PID "$GOLDEN_PROCESS" //T //F >/dev/null 2>&1; kill -KILL "$GOLDEN_PROCESS" 2>/dev/null' TERM INT HUP
  wait "$GOLDEN_PROCESS"
  DRIVER_STATUS=$?
  trap - TERM INT HUP
  exit "$DRIVER_STATUS"
fi

if [ "${1-}" = --bounded ]; then
  shift
  GOLDEN=$1
  TIMEOUT_SECONDS=${CLI_TEST_TIMEOUT_SECONDS:-60}
  GOLDEN_PROCESS=
  GOLDEN_SESSION=
  LAUNCH_PROCESS=
  GOLDEN_SESSION_FILE=
  GOLDEN_STATUS_FILE=
  HOST_SYSTEM=$(uname -s)
  CLEANUP_IS_ARMED=yes
  PENDING_EXIT_STATUS=

  case $TIMEOUT_SECONDS in
  ''|*[!0-9]*|0)
    printf 'invalid CLI golden timeout\n'
    exit 125
    ;;
  esac

  if [ "${OS-}" = Windows_NT ]; then
    run_test_with_timeout "$TIMEOUT_SECONDS" \
      "$TEST_SHELL_COMMAND" ./run-cli-test.sh --windows-driver "$GOLDEN"
    exit $?
  fi

  list_golden_session_processes()
  {
    if [ "$HOST_SYSTEM" = Linux ]; then
      for PROCESS_STAT_PATH in /proc/[0-9]*/stat; do
        [ -r "$PROCESS_STAT_PATH" ] || continue
        IFS= read -r PROCESS_STAT 2>/dev/null < "$PROCESS_STAT_PATH" || continue
        PROCESS_FIELDS=${PROCESS_STAT##*) }
        set -- $PROCESS_FIELDS
        [ "$#" -ge 4 ] || continue
        [ "$1" = Z ] && continue
        [ "$4" = "$GOLDEN_SESSION" ] || continue
        PROCESS_ID=${PROCESS_STAT_PATH#/proc/}
        printf '%s\n' "${PROCESS_ID%/stat}"
      done
      return
    fi

    if PROCESS_TABLE=$(ps -Ao pid=,sess= 2>/dev/null); then
      :
    elif PROCESS_TABLE=$(ps -Ao pid=,sid= 2>/dev/null); then
      :
    else
      return 1
    fi
    printf '%s\n' "$PROCESS_TABLE" | while read -r PROCESS_ID PROCESS_SESSION; do
      if [ "$PROCESS_SESSION" = "$GOLDEN_SESSION" ]; then
        printf '%s\n' "$PROCESS_ID"
      fi
    done
  }

  terminate_golden_tree()
  {
    DISCOVERY_FAILED=no
    SESSION_PROCESSES=$(list_golden_session_processes) || {
      DISCOVERY_FAILED=yes
      SESSION_PROCESSES=
    }
    if [ -n "$GOLDEN_PROCESS$SESSION_PROCESSES" ]; then
      kill -TERM $GOLDEN_PROCESS $SESSION_PROCESSES 2>/dev/null || true
    fi
    sleep 0.1
    SESSION_PROCESSES=$(list_golden_session_processes) || {
      DISCOVERY_FAILED=yes
      SESSION_PROCESSES=
    }
    if [ -n "$GOLDEN_PROCESS$SESSION_PROCESSES" ]; then
      kill -KILL $GOLDEN_PROCESS $SESSION_PROCESSES 2>/dev/null || true
    fi
    [ "$DISCOVERY_FAILED" = no ]
  }

  cleanup_golden_tree()
  {
    if [ "$CLEANUP_IS_ARMED" = yes ] && [ -n "$GOLDEN_SESSION" ]; then
      if ! terminate_golden_tree; then
        printf 'golden session discovery failed\n'
      fi
      if [ -n "$LAUNCH_PROCESS" ]; then
        wait "$LAUNCH_PROCESS" 2>/dev/null || true
      fi
    fi

    if [ -n "$GOLDEN_SESSION_FILE" ]; then
      rm -f "$GOLDEN_SESSION_FILE"
    fi

    if [ -n "$GOLDEN_STATUS_FILE" ]; then
      rm -f "$GOLDEN_STATUS_FILE"
    fi
  }

  request_exit()
  {
    PENDING_EXIT_STATUS=$1
    if [ -n "$GOLDEN_SESSION" ]; then
      exit "$PENDING_EXIT_STATUS"
    fi
  }

  trap cleanup_golden_tree EXIT
  trap 'request_exit 130' INT
  trap 'request_exit 143' TERM
  trap 'request_exit 129' HUP

  GOLDEN_SESSION_FILE=$(mktemp) || GOLDEN_SESSION_FILE=
  GOLDEN_STATUS_FILE=$(mktemp) || GOLDEN_STATUS_FILE=
  if [ -z "$GOLDEN_SESSION_FILE" ] || [ -z "$GOLDEN_STATUS_FILE" ]; then
    printf 'cannot create a CLI golden session\n'
    exit 125
  fi

  GOLDEN_LAUNCHER='printf "%s\n" "$$" > "$GOLDEN_SESSION_FILE"
  "$TEST_SHELL_COMMAND" "$1"
  printf "%s\n" "$?" > "$GOLDEN_STATUS_FILE"'

  set -m
  if command -v setsid >/dev/null 2>&1; then
    BIN=$BIN TEST_SHELL_COMMAND=$TEST_SHELL_COMMAND \
      GOLDEN_SESSION_FILE=$GOLDEN_SESSION_FILE \
      GOLDEN_STATUS_FILE=$GOLDEN_STATUS_FILE \
      setsid /bin/sh -c "$GOLDEN_LAUNCHER" golden "$GOLDEN" &
  elif command -v perl >/dev/null 2>&1; then
    BIN=$BIN TEST_SHELL_COMMAND=$TEST_SHELL_COMMAND \
      GOLDEN_SESSION_FILE=$GOLDEN_SESSION_FILE \
      GOLDEN_STATUS_FILE=$GOLDEN_STATUS_FILE \
      perl -MPOSIX -e 'POSIX::setsid(); exec @ARGV' \
      /bin/sh -c "$GOLDEN_LAUNCHER" golden "$GOLDEN" &
  else
    printf 'cannot create a CLI golden session\n'
    exit 125
  fi
  LAUNCH_PROCESS=$!
  set +m

  ATTEMPT_COUNT=0
  while [ ! -s "$GOLDEN_SESSION_FILE" ] && [ "$ATTEMPT_COUNT" -lt 100 ]; do
    sleep 0.1
    ATTEMPT_COUNT=$((ATTEMPT_COUNT + 1))
  done

  if [ ! -s "$GOLDEN_SESSION_FILE" ]; then
    printf 'cannot create a CLI golden session\n'
    kill -KILL "$LAUNCH_PROCESS" 2>/dev/null || true
    wait "$LAUNCH_PROCESS" 2>/dev/null || true
    exit 125
  fi

  IFS= read -r GOLDEN_SESSION < "$GOLDEN_SESSION_FILE"
  GOLDEN_PROCESS=$GOLDEN_SESSION
  if [ -n "$PENDING_EXIT_STATUS" ]; then
    exit "$PENDING_EXIT_STATUS"
  fi

  ATTEMPT_COUNT=0
  ATTEMPT_LIMIT=$((TIMEOUT_SECONDS * 10))
  while [ ! -s "$GOLDEN_STATUS_FILE" ] &&
    [ "$ATTEMPT_COUNT" -lt "$ATTEMPT_LIMIT" ]; do
    sleep 0.1
    ATTEMPT_COUNT=$((ATTEMPT_COUNT + 1))
  done

  if [ ! -s "$GOLDEN_STATUS_FILE" ]; then
    printf 'golden timed out\n'
    TERMINATION_STATUS=0
    terminate_golden_tree || TERMINATION_STATUS=$?
    wait "$LAUNCH_PROCESS" 2>/dev/null || true
    GOLDEN_PROCESS=
    LAUNCH_PROCESS=
    CLEANUP_IS_ARMED=no
    if [ "$TERMINATION_STATUS" -ne 0 ]; then
      printf 'golden session discovery failed\n'
      exit 125
    fi
    exit 124
  fi

  IFS= read -r GOLDEN_STATUS < "$GOLDEN_STATUS_FILE"
  case $GOLDEN_STATUS in
  ''|*[!0-9]*)
    printf 'golden status is unreadable\n'
    terminate_golden_tree 2>/dev/null || true
    CLEANUP_IS_ARMED=no
    exit 125
    ;;
  esac

  wait "$LAUNCH_PROCESS" 2>/dev/null || true
  GOLDEN_PROCESS=
  LAUNCH_PROCESS=
  ATTEMPT_COUNT=0
  HAS_LIVING_DESCENDANT=yes
  SESSION_DISCOVERY_FAILED=no
  while [ "$HAS_LIVING_DESCENDANT" = yes ] && [ "$ATTEMPT_COUNT" -lt 1000 ]; do
    HAS_LIVING_DESCENDANT=no
    SESSION_PROCESSES=$(list_golden_session_processes) || {
      SESSION_DISCOVERY_FAILED=yes
      break
    }
    for PROCESS_ID in $SESSION_PROCESSES; do
      if [ -n "$PROCESS_ID" ]; then
        HAS_LIVING_DESCENDANT=yes
        break
      fi
    done
    if [ "$HAS_LIVING_DESCENDANT" = yes ]; then
      sleep 0.01
      ATTEMPT_COUNT=$((ATTEMPT_COUNT + 1))
    fi
  done
  if [ "$SESSION_DISCOVERY_FAILED" = yes ]; then
    printf 'golden session discovery failed\n'
    terminate_golden_tree 2>/dev/null || true
    CLEANUP_IS_ARMED=no
    exit 125
  fi
  if [ "$HAS_LIVING_DESCENDANT" = yes ]; then
    printf 'golden leaked processes\n'
    terminate_golden_tree
    CLEANUP_IS_ARMED=no
    exit 125
  fi

  CLEANUP_IS_ARMED=no
  exit "$GOLDEN_STATUS"
fi

REFILL_MODE=no
TEST_STATUS=0
if [ "${1-}" = --refill ]; then
  REFILL_MODE=yes
  shift
fi

TEST_SHELL_COMMAND=$1
shift

for TEST_FILE in "$@"; do
  TEST_NAME=$(basename "$TEST_FILE" .sh)
  if [ "$REFILL_MODE" = yes ]; then
    OUTPUT="expected/.$TEST_NAME.out.tmp"
  else
    OUTPUT_DIRECTORY="$TEST_TEMP_DIRECTORY/results/cli"
    mkdir -p "$OUTPUT_DIRECTORY"
    OUTPUT="$OUTPUT_DIRECTORY/$TEST_NAME.out"
  fi

  GOLDEN_TIMEOUT_SECONDS=60
  if [ "$TEST_NAME" = history_behavior ] || [ "$TEST_NAME" = koshkit_timeout ]; then
    GOLDEN_TIMEOUT_SECONDS=120
  fi
  CLI_TEST_TIMEOUT_SECONDS=${CLI_TEST_TIMEOUT_SECONDS:-$GOLDEN_TIMEOUT_SECONDS} \
    BIN="$BIN" TEST_SHELL_COMMAND="$TEST_SHELL_COMMAND" \
    "$TEST_SHELL_COMMAND" ./run-cli-test.sh --bounded \
    "$TEST_FILE" > "$OUTPUT" 2>&1
  DRIVER_STATUS=$?

  if is_driver_status_harness_failure "$DRIVER_STATUS" "$REFILL_MODE"; then
    command cat "$OUTPUT"
    printf "\t%-64s harness failure, status %s\n" \
      "cli/$TEST_NAME.sh" "$DRIVER_STATUS"
    rm -f "$OUTPUT"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=$DRIVER_STATUS
    fi
    continue
  fi

  if [ "$REFILL_MODE" = yes ]; then
    mv "$OUTPUT" "expected/$TEST_NAME.out"
    printf "\t%-64s cli/%s.out\n" "cli/$TEST_NAME.sh" "$TEST_NAME"
    continue
  fi

  if diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" >/dev/null 2>&1; then
    printf "\t%-64s ok\033[K\r" "cli/$TEST_NAME.sh"
  else
    set_golden_failure_file "cli-$TEST_NAME"
    diff $DIFF_FLAGS "expected/$TEST_NAME.out" "$OUTPUT" | \
      tee -a "$GOLDEN_FAILURE_FILE"
    printf "\t%-64s FAILED :c (driver status %s)\n" \
      "cli/$TEST_NAME.sh" "$DRIVER_STATUS"
    if [ "$TEST_STATUS" -eq 0 ]; then
      TEST_STATUS=1
    fi
  fi
  rm -f "$OUTPUT"
done

exit "$TEST_STATUS"
