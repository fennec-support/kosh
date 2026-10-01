word_is_listed()
{
  WORD=$1
  WORDS=$2

  case " $WORDS " in
  *" $WORD "*) return 0 ;;
  *) return 1 ;;
  esac
}

ACTIVE_TEST_NAMES=
for TEST_FILE in kosh/*.kosh; do
  TEST_NAME=${TEST_FILE#kosh/}
  TEST_NAME=${TEST_NAME%.kosh}
  if word_is_listed "$TEST_NAME" "$SKIPPED_TEST_NAMES"; then
    continue
  fi
  ACTIVE_TEST_NAMES="$ACTIVE_TEST_NAMES $TEST_NAME"
done

SERIAL_CLI_CANDIDATES="cli/set_option_state.sh cli/read_behavior.sh"
PARALLEL_CLI_INPUT=
SERIAL_CLI_INPUT=
for TEST_FILE in cli/*.sh; do
  if word_is_listed "$TEST_FILE" "$SKIPPED_CLI_INPUT"; then
    continue
  fi
  if [ -n "${SKIP_CLI_ASSIMILATE-}" ] && \
    [ "$TEST_FILE" = cli/assimilate.sh ]; then
    continue
  fi
  if word_is_listed "$TEST_FILE" "$SERIAL_CLI_CANDIDATES"; then
    SERIAL_CLI_INPUT="$SERIAL_CLI_INPUT $TEST_FILE"
  else
    PARALLEL_CLI_INPUT="$PARALLEL_CLI_INPUT $TEST_FILE"
  fi
done

SERIAL_COMPLETION_CANDIDATES=completion/editor_append_hot_path.sh
PARALLEL_COMPLETION_INPUT=
SERIAL_COMPLETION_INPUT=
for TEST_FILE in completion/*.sh; do
  if word_is_listed "$TEST_FILE" "$SKIPPED_COMPLETION_INPUT" || \
    word_is_listed "$TEST_FILE" "$UNREPRESENTABLE_COMPLETION_INPUT"; then
    continue
  fi
  case $TEST_FILE in
  completion/*help*) IS_SERIAL_COMPLETION=yes ;;
  *) IS_SERIAL_COMPLETION=no ;;
  esac
  if word_is_listed "$TEST_FILE" "$SERIAL_COMPLETION_CANDIDATES" || \
    [ "$IS_SERIAL_COMPLETION" = yes ]; then
    SERIAL_COMPLETION_INPUT="$SERIAL_COMPLETION_INPUT $TEST_FILE"
  else
    PARALLEL_COMPLETION_INPUT="$PARALLEL_COMPLETION_INPUT $TEST_FILE"
  fi
done

HIGHLIGHT_INPUT=$(printf '%s ' highlight/*.sh)
SH_COMPAT_FILES=
for TEST_FILE in sh/*.sh; do
  case $TEST_FILE in
  *_1.sh) ;;
  *) SH_COMPAT_FILES="$SH_COMPAT_FILES $TEST_FILE" ;;
  esac
done
BASH_COMPAT_FILES=
for TEST_FILE in bash/*.bash; do
  case $TEST_FILE in
  *_1.bash) ;;
  *) BASH_COMPAT_FILES="$BASH_COMPAT_FILES $TEST_FILE" ;;
  esac
done
