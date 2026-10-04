#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

# Run:
#     curl -fsSL "https://fennec.support/kosh/install" | sh

{
  set -eu
  RELEASES=https://github.com/toiletbril/kosh/releases
  IS_COLOR=0
  IS_ERROR_COLOR=0
  SYSTEM=
  SYSTEM_NAME=
  ARCH=
  ARCH_NAME=
  EXT=
  IS_DRY_RUN=0
  BIN_DIR=${KOSH_INSTALL_PATH:-}

  if [ -t 1 ] && [ -z "${NO_COLOR:-}" ]
  then
    IS_COLOR=1
  fi

  if [ -t 2 ] && [ -z "${NO_COLOR:-}" ]
  then
    IS_ERROR_COLOR=1
  fi

  paint ()
  {
    if [ "$1" -eq 1 ]
    then
      printf '\033[%sm%s\033[0m' "$2" "$3"
    else
      printf '%s' "$3"
    fi
  }

  mark ()
  {
    paint "$1" '1;95' ':3c'
    printf ' '
    paint "$1" '1;97' '+'
    printf ' '
  }

  fail ()
  {
    {
      printf 'kosh installer: '
      paint "$IS_ERROR_COLOR" '1;91' 'error:'
      printf ' %s.\n' "$1"
    } >&2
    exit 1
  }

  label ()
  {
    mark "$IS_COLOR"
    paint "$IS_COLOR" 1 "$1"
    if [ "$#" -ge 2 ]
    then
      printf ' %s' "$2"
    fi
  }

  step ()
  {
    label "$@"
    printf ' '
  }

  say ()
  {
    label "$@"
    printf '\n'
  }

  line ()
  {
    mark "$IS_COLOR"
    printf '%s\n' "$1"
  }

  blue ()
  {
    paint "$IS_COLOR" '1;34' "$1"
  }

  result ()
  {
    printf '%s\n' "$1"
  }

  fetch ()
  {
    curl --proto '=https' --tlsv1.2 --retry 3 -fsSL "$@"
  }

  ask ()
  {
    if ! (: </dev/tty) 2>/dev/null
    then
      return 0
    fi
    {
      mark "$IS_ERROR_COLOR"
      printf '%s [y/n] ' "$1"
    } >&2
    ANSWER=
    read -r ANSWER </dev/tty || :
    case $ANSWER in
      [Nn]*)
        return 1
      ;;
      *)
        return 0
      ;;
    esac
  }

  while [ $# -gt 0 ]
  do
    case $1 in
      --dry-run)
        IS_DRY_RUN=1
      ;;
      --install-path)
        [ $# -ge 2 ] || fail "--install-path needs a path"
        BIN_DIR=$2
        shift
      ;;
      --install-path=*)
        BIN_DIR=${1#--install-path=}
      ;;
      *)
        fail "unknown option $1"
      ;;
    esac
    shift
  done

  if [ -n "${KOSH_INSTALL_DRY_RUN:-}" ]
  then
    IS_DRY_RUN=1
  fi

  say "Hi!"
  command -v curl >/dev/null || fail "curl is required"
  case $(uname -s) in
    Linux)
      SYSTEM=linux SYSTEM_NAME=Linux EXT=
    ;;
    Darwin)
      SYSTEM=darwin SYSTEM_NAME=macOS EXT=
    ;;
    MINGW* | MSYS* | CYGWIN*)
      SYSTEM=win32 SYSTEM_NAME=Windows EXT=.exe
    ;;
    *)
      fail "unsupported system $(uname -s)"
    ;;
  esac

  case $(uname -m) in
    x86_64 | amd64)
      ARCH=amd64 ARCH_NAME=AMD64
    ;;
    aarch64 | arm64)
      ARCH=aarch64 ARCH_NAME=ARM64
    ;;
    *)
      fail "unsupported processor $(uname -m)"
    ;;
  esac

  if [ "$(sysctl -n sysctl.proc_translated 2>/dev/null || :)" = 1 ]
  then
    ARCH=aarch64 ARCH_NAME=ARM64
  fi

  say "Detecting the system.." "$SYSTEM_NAME on $ARCH_NAME"
  VERSION=${KOSH_INSTALL_VERSION:-}

  if [ -z "$VERSION" ]
  then
    LATEST=$(fetch -o /dev/null -w '%{url_effective}' "$RELEASES/latest")
    VERSION=${LATEST##*/}
  fi

  case $VERSION in
    '' | latest | releases | *[!A-Za-z0-9._-]*)
      fail "invalid release version '$VERSION'"
    ;;
    *)
    ;;
  esac

  say "Looking up the latest release.." "$VERSION"

  if [ -z "$BIN_DIR" ]
  then
    BIN_DIR=$HOME/.local/bin
    if [ "$(id -u)" -eq 0 ]
    then
      BIN_DIR=/usr/local/bin
    fi
  fi

  case $BIN_DIR in
    /*)
    ;;
    *)
      BIN_DIR=$PWD/$BIN_DIR
    ;;
  esac

  while [ "${BIN_DIR%/}" != "$BIN_DIR" ] && [ "$BIN_DIR" != / ]
  do
    BIN_DIR=${BIN_DIR%/}
  done
  SHARE_DIR=$(dirname -- "$BIN_DIR")/share

  BINARY=kosh-$SYSTEM-$ARCH-$VERSION$EXT
  FILES=$BINARY
  IS_EXTRAS_WANTED=0

  if [ "$IS_DRY_RUN" -eq 0 ]
  then
    if ! ask "Do you want to install Kosh $VERSION to $BIN_DIR?"
    then
      say "That's a shame. Specify another path via --install-path :c"
      exit 0
    fi
    if ask "Do you want to install completions and manpages to $SHARE_DIR?"
    then
      IS_EXTRAS_WANTED=1
    fi
  else
    IS_EXTRAS_WANTED=1
  fi

  if [ "$IS_EXTRAS_WANTED" -eq 1 ]
  then
    FILES="$FILES kosh.bash"
    if command -v zstd >/dev/null
    then
      FILES="$FILES kosh.1.zst kosh.5.zst"
    fi
  fi

  WORK=$(mktemp -d)

  trap 'rm -rf "$WORK"' EXIT
  trap 'exit 130' INT TERM
  cd "$WORK" || fail "cannot enter $WORK"
  URL=$RELEASES/download/$VERSION

  fetch -o SHA256SUMS "$URL/SHA256SUMS" ||
  fail "release $VERSION has no SHA256SUMS file"
  grep -q " \*\{0,1\}$BINARY\$" SHA256SUMS ||
  fail "release $VERSION has no build for $SYSTEM on $ARCH"

  if [ "$IS_DRY_RUN" -eq 1 ]
  then
    for FILE in $FILES
    do
      line "Would download $URL/$FILE and verify it against SHA256SUMS"
    done
    line "Would install $BIN_DIR/kosh$EXT"
    line "Would install completions and manpages to $SHARE_DIR"
    line "Dry run: nothing was installed."
    exit 0
  fi

  for FILE in $FILES
  do
    step "Downloading" "$(blue "$FILE").."
    if [ -t 2 ]
    then
      result ""
      fetch -# -o "$FILE" "$URL/$FILE" || fail "unable to download $FILE"
    else
      fetch -o "$FILE" "$URL/$FILE" || fail "unable to download $FILE"
      result "100%"
    fi
    grep " \*\{0,1\}$FILE\$" SHA256SUMS >>expected
  done

  step "Verifying the binaries.."
  IS_BAD=0
  if command -v sha256sum >/dev/null
  then
    sha256sum -c expected >/dev/null 2>&1 || IS_BAD=1
  else
    shasum -a 256 -c expected >/dev/null 2>&1 || IS_BAD=1
  fi
  if [ "$IS_BAD" -eq 1 ]
  then
    result "not ok"
    fail "checksum mismatch"
  fi
  result "ok"

  mkdir -p "$BIN_DIR" || fail "cannot write to $BIN_DIR"
  install -m 755 "$BINARY" "$BIN_DIR/kosh$EXT"
  if [ "$IS_EXTRAS_WANTED" -eq 1 ]
  then
    mkdir -p "$SHARE_DIR/bash-completion/completions" ||
    fail "cannot write to $SHARE_DIR"
    install -m 644 kosh.bash "$SHARE_DIR/bash-completion/completions/kosh"
    if [ -f kosh.1.zst ]
    then
      mkdir -p "$SHARE_DIR/man/man1" "$SHARE_DIR/man/man5"
      zstd -dq kosh.1.zst kosh.5.zst
      install -m 644 kosh.1 "$SHARE_DIR/man/man1/kosh.1"
      install -m 644 kosh.5 "$SHARE_DIR/man/man5/kosh.5"
    fi
  fi

  say "Meow meow meow (success!)." "$BIN_DIR/kosh$EXT"
  case ":$PATH:" in
    *":$BIN_DIR:"*)
      line "Use $(blue kosh) to launch the shell."
    ;;
    *)
      line "Use $BIN_DIR/kosh$EXT to launch the shell."
    ;;
  esac

  exit 0
}
