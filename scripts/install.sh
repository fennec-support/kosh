#!/bin/sh

# Koshka shell installer.
#
# Copyright 2026 toiletbril
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
# POSSIBILITY OF SUCH DAMAGE.

# Run:
#     curl -fsSL "https://fennec.support/kosh/install" | sh

{
  set -eu
  RELEASES=https://github.com/toiletbril/kosh/releases
  BIN_DIR=${KOSH_INSTALL_PATH:-}
  IS_DRY_RUN=${KOSH_INSTALL_DRY_RUN:+1}
  IS_COLOR=
  IS_ERROR_COLOR=

  if [ -z "${NO_COLOR:-}" ]
  then
    if [ -t 1 ]
    then
      IS_COLOR=1
    fi

    if [ -t 2 ]
    then
      IS_ERROR_COLOR=1
    fi
  fi

  paint ()
  {
    if [ -n "$1" ]
    then
      printf '\033[%sm%s\033[0m' "$2" "$3"
    else
      printf '%s' "$3"
    fi
  }

  mark ()
  {
    printf '%s ' "$(paint "$1" '1;95' ':3c') "
  }

  label ()
  {
    printf '%s%s%s' "$(mark "$IS_COLOR")" "$(paint "$IS_COLOR" 1 "$1")" \
      "${2:+ $2}"
  }

  say ()
  {
    label "$@"
    printf '\n'
  }

  step ()
  {
    label "$@"
    printf ' '
  }

  line ()
  {
    printf '%s%s\n' "$(mark "$IS_COLOR")" "$1"
  }

  fail ()
  {
    printf 'kosh installer: %s %s.\n' \
      "$(paint "$IS_ERROR_COLOR" '1;91' error:)" "$1" >&2
    exit 1
  }

  ask ()
  {
    # shellcheck disable=SC2217
    if ! true 2>/dev/null </dev/tty
    then
      return 0
    fi

    printf '%s%s [y/n] ' "$(mark "$IS_ERROR_COLOR")" "$1" >&2
    case $(head -n 1 </dev/tty || :) in
      [Nn]*)
        return 1
      ;;
      *)
        return 0
      ;;
    esac
  }

  fetch ()
  {
    curl --proto '=https' --tlsv1.2 --retry 3 -fsSL "$@"
  }

  progress ()
  {
    if [ -f "$1" ] && [ "$2" -gt 0 ]
    then
      set -- "$(($(wc -c <"$1") * 100 / $2))"
    else
      set -- 0
    fi

    if [ "$1" -gt 99 ]
    then
      set -- 99
    fi

    printf '%3d%%\b\b\b\b' "$1"
  }

  content_length ()
  {
    fetch -I "$1" | tr -d '\r' | sed -n 's/^[Cc]ontent-[Ll]ength: *//p' |
    tail -n 1
  }

  while [ "$#" -gt 0 ]
  do
    case $1 in
      --dry-run)
        IS_DRY_RUN=1
      ;;
      --install-path=*)
        BIN_DIR=${1#*=}
        [ -n "$BIN_DIR" ] || fail "--install-path needs a path"
      ;;
      --install-path)
        [ "$#" -ge 2 ] || fail "--install-path needs a path"
        case $2 in
          '' | -*)
            fail "--install-path needs a path, not '$2'"
          ;;
          *)
          ;;
        esac

        BIN_DIR=$2

        shift
      ;;
      -x)
        set -x
      ;;
      -h | --help)
        line "Usage: install.sh [--dry-run] [--install-path DIR] [-x]"
        line "  --dry-run           list the downloads, install nothing"
        line "  --install-path DIR  install to DIR, extras to DIR/../share"
        line "  -x                  trace every command"
        line "  --help              print this help"
        line "Environment: KOSH_INSTALL_PATH, KOSH_INSTALL_VERSION,"
        line "  KOSH_INSTALL_DRY_RUN, NO_COLOR"
        exit 0
      ;;
      *)
        fail "unknown option $1"
      ;;
    esac

    shift
  done

  say "Hi! This is Koshka Shell installer."
  say \
    "You can view the repository and this script at <github.com/toiletbril/kosh>"
  command -v curl >/dev/null || fail "curl is required"
  EXT=

  case $(uname -s) in
    Linux)
      SYSTEM=linux SYSTEM_NAME=Linux
    ;;
    Darwin)
      SYSTEM=darwin SYSTEM_NAME=macOS
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
    VERSION=$(fetch -o /dev/null -w '%{url_effective}' "$RELEASES/latest")
    VERSION=${VERSION##*/}
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

  BIN_DIR=${BIN_DIR%/}
  SHARE_DIR=${BIN_DIR%/*}/share
  BINARY=kosh-$SYSTEM-$ARCH-$VERSION$EXT
  FILES="$BINARY kosh.bash"

  if [ -z "$IS_DRY_RUN" ]
  then
    if ! ask "Do you want to install Kosh $VERSION to $BIN_DIR?"
    then
      say "That's a shame. Specify another path via --install-path :c"
      exit 0
    fi

    if ! ask "Do you want to install completions and manpages to $SHARE_DIR?"
    then
      FILES=$BINARY
    fi
  fi

  if [ "$FILES" != "$BINARY" ] && command -v zstd >/dev/null
  then
    FILES="$FILES kosh.1.zst kosh.5.zst"
  fi

  WORK=$(mktemp -d)

  trap 'kill ${PID:-} 2>/dev/null; rm -rf "$WORK"' EXIT
  trap 'exit 130' INT TERM
  cd "$WORK" || fail "cannot enter $WORK"
  URL=$RELEASES/download/$VERSION

  fetch -o SHA256SUMS "$URL/SHA256SUMS" ||
  fail "release $VERSION has no SHA256SUMS file"
  grep -q " \*\{0,1\}$BINARY\$" SHA256SUMS ||
  fail "release $VERSION has no build for $SYSTEM on $ARCH"

  for FILE in $FILES
  do
    grep -q " \*\{0,1\}$FILE\$" SHA256SUMS ||
    fail "release $VERSION has no checksum for $FILE"
  done

  if [ -n "$IS_DRY_RUN" ]
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
    step Downloading "$(paint "$IS_COLOR" '1;34' "$FILE").."
    if [ -t 1 ]
    then
      SIZE=$(content_length "$URL/$FILE")

      fetch -o "$FILE" "$URL/$FILE" &
      PID=$!

      while kill -0 "$PID" 2>/dev/null
      do
        progress "$FILE" "${SIZE:-0}"
        sleep 0.1
      done

      wait "$PID" || fail "unable to download $FILE"
    else
      fetch -o "$FILE" "$URL/$FILE" || fail "unable to download $FILE"
    fi

    printf '100%%\n'
    grep " \*\{0,1\}$FILE\$" SHA256SUMS >>expected
  done

  step "Verifying the binaries.."
  CHECK="sha256sum -c"

  if ! command -v sha256sum >/dev/null
  then
    CHECK="shasum -a 256 -c"
  fi

  if ! $CHECK expected >/dev/null 2>&1
  then
    printf 'not ok\n'
    fail "checksum mismatch"
  fi

  printf 'ok\n'
  mkdir -p "$BIN_DIR" || fail "cannot write to $BIN_DIR"
  install -m 755 "$BINARY" "$BIN_DIR/kosh$EXT"
  if [ "$FILES" != "$BINARY" ]
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

  say "Success! Meow meow meow. Your binary is here:" "$BIN_DIR/kosh$EXT"
  case ":$PATH:" in
    *":$BIN_DIR:"*)
      line "Use $(paint "$IS_COLOR" '1;34' kosh) to launch the shell."
    ;;
    *)
      line "Install prefix is not in the PATH. Use $(paint "$IS_COLOR" '1;34' $BIN_DIR/kosh$EXT) to launch the shell."
    ;;
  esac
}


