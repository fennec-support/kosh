directory=
fail()
{
  echo "failed at line $1"
  exit 1
}

cleanup()
{
  if [ -n "$directory" ]; then
    "$BIN_DIR/invoke-koshkit" rm -rf -- "$directory"
  fi
}
trap cleanup EXIT

if [ "${OS-}" = Windows_NT ]; then
  directory=$(mktemp -d) || fail "$LINENO"
  path_value='C:\clear\e[2J\tail'
  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c 'echo "$PATH"; echo survived')
  expected=$(printf '%s\n%s' "$path_value" survived)
  [ "$output" = "$expected" ] || fail "$LINENO"

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c '# shellcheck disable=search-path-overwritten
PATH="C:\updated"; koshkit env | koshkit grep "^Path="')
  [ "$output" = 'Path=C:\updated' ] || fail "$LINENO"

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c 'export path=first; export PATH=second; \
printf "%s %s\n" "${path@a}" "${PATH@a}"')
  [ "$output" = 'x x' ] || fail "$LINENO"

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'printf "%s %s %s\n" "${MyImported@a}" "${MYIMPORTED@a}" "${myimported@a}"')
  [ "$output" = 'x x x' ] || fail "$LINENO"

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'export -n MYIMPORTED; MyImported=kept; \
printf "[%s][%s]\n" "${MyImported@a}" "${MYIMPORTED@a}"')
  [ "$output" = '[][]' ] || fail "$LINENO"

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'set -u; name=MyImportedd; echo "${!name}"' 2>&1)
  case "$output" in
    *"The variable 'MyImported' is set"*) ;;
    *) fail "$LINENO" ;;
  esac

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    myimported=one "$BIN" --no-init-files -c \
    'set -u; name=myimportedd; echo "${!name}"' 2>&1)
  case "$output" in
    *"The variable 'myimported' is set"*) ;;
    *) fail "$LINENO" ;;
  esac

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    MyImported=one "$BIN" --no-init-files -c \
    'export MYIMPORTED=two; set -u; name=MyImportedd; echo "${!name}"' 2>&1)
  case "$output" in
    *"The variable 'MyImported' is set"*) ;;
    *) fail "$LINENO" ;;
  esac

  output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" --no-init-files -c \
    'OtherVar=zero; (export OTHERVAR=two); printf "[%s]\n" "${OtherVar@a}"')
  [ "$output" = '[]' ] || fail "$LINENO"

  printf '@echo off\r\necho path-refresh-ran\r\n' > "$directory/path-refresh.bat"
  if ! output=$(env -u PATH "$TEST_PATH_ENVIRONMENT_NAME=$path_value" \
    "$BIN" -c 'Path="$1"; path-refresh' path-test "$directory")
  then
    fail "$LINENO"
  fi
  [ "$output" = path-refresh-ran ] || fail "$LINENO"

  [ "$("$BIN" --no-annoying-diagnostics -c 'printf "%s" C:\new')" = 'C:new' ] || fail "$LINENO"
  [ "$("$BIN" -c "printf '%s' 'C:\new'")" = 'C:\new' ] || fail "$LINENO"
  [ "$("$BIN" -c 'printf "%s" C:\\new')" = 'C:\new' ] || fail "$LINENO"
  if "$BIN" --debug-highlight-at '' </dev/null >/dev/null 2>&1; then
    case "$("$BIN" --debug-highlight-at 'echo C:\Windows')" in
      *'C:\Windows'*) fail "$LINENO" ;;
    esac
  fi
fi

# The exported name store folds case on Windows and keeps it everywhere else,
# so the imported name is reachable through its uppercase spelling only there.
if [ "${OS-}" = Windows_NT ]; then
  expected_folded_lookup='[one][one]'
else
  expected_folded_lookup='[one][unset]'
fi
folded_lookup=$(MyFolded=one "$BIN" --no-init-files -c \
  'printf "[%s][%s]" "${MyFolded-unset}" "${MYFOLDED-unset}"')
[ "$folded_lookup" = "$expected_folded_lookup" ] || fail "$LINENO"

[ "$("$BIN" -c 'echo -e "a\tb"')" = "$(printf 'a\tb')" ] || fail "$LINENO"
echo "Exported name folding follows the platform"
echo "Windows PATH echo stays literal"
echo "Windows paths retain the portable shell escape grammar"
