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
  RELEASES=https://github.com/fennec-support/kosh/releases
  BIN_DIR=${KOSH_INSTALL_PATH:-}
  IS_DRY_RUN=${KOSH_INSTALL_DRY_RUN:+1}
  IS_FORCE=${KOSH_INSTALL_FORCE:+1}
  IS_YES=
  IS_UNINSTALL=
  LINK_MOODS=
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
    if [ -n "$IS_YES" ]; then return 0; fi
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

  # Removal is confirmed only by an explicit yes, and without a terminal it
  # needs --yes.
  ask_strict ()
  {
    if [ -n "$IS_YES" ]; then return 0; fi
    # shellcheck disable=SC2217
    if ! true 2>/dev/null </dev/tty
    then
      return 1
    fi

    printf '%s%s [y/N] ' "$(mark "$IS_ERROR_COLOR")" "$1" >&2
    case $(head -n 1 </dev/tty || :) in
      [Yy] | [Yy][Ee][Ss])
        return 0
      ;;
      *)
        return 1
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

  installed_kosh ()
  {
    if [ -x "$BIN_DIR/kosh$EXT" ]
    then
      printf '%s' "$BIN_DIR/kosh$EXT"
    fi
  }

  add_link_moods ()
  {
    [ -n "$1" ] || fail "--link-mood needs bash, dash, sh, or kosh"
    case $1 in
      ,* | *, | *,,*)
        fail "--link-mood needs bash, dash, sh, or kosh, not '$1'"
      ;;
      *)
      ;;
    esac

    REMAINING=$1

    while [ -n "$REMAINING" ]
    do
      MOOD=${REMAINING%%,*}

      case $REMAINING in
        *,*)
          REMAINING=${REMAINING#*,}
        ;;
        *)
          REMAINING=
        ;;
      esac

      case $MOOD in
        bash | dash | sh)
          LINK_MOODS="$LINK_MOODS $MOOD"
        ;;
        kosh)
        ;;
        *)
          fail "--link-mood needs bash, dash, sh, or kosh, not '$MOOD'"
        ;;
      esac
    done
  }

  is_free_for_link ()
  {
    if [ ! -e "$1" ] && [ ! -L "$1" ]
    then
      return 0
    fi

    [ -L "$1" ] || return 1
    case $(readlink "$1" || :) in
      kosh | "$BIN_DIR/kosh")
        return 0
      ;;
    esac

    return 1
  }

  installed_version ()
  {
    "$1" --version 2>/dev/null | sed -n '1s/^Koshka Shell //p'
  }

  is_at_least ()
  {
    awk -v HAVE="$1" -v WANT="$2" 'BEGIN {
      split(HAVE, H, /[^0-9]+/)
      split(WANT, W, /[^0-9]+/)
      for (I = 1; I <= 3; I++) {
        if (H[I] + 0 != W[I] + 0) {
          exit !(H[I] + 0 > W[I] + 0)
        }
      }
      exit (HAVE ~ /-/ && WANT !~ /-/)
    }'
  }

  while [ "$#" -gt 0 ]
  do
    case $1 in
      --dry-run)
        IS_DRY_RUN=1
      ;;
      --force)
        IS_FORCE=1
      ;;
      --yes)
        IS_YES=1
      ;;
      --uninstall)
        IS_UNINSTALL=1
      ;;
      --link-mood=*)
        add_link_moods "${1#*=}"
      ;;
      --link-mood)
        [ "$#" -ge 2 ] || fail "--link-mood needs bash, dash, sh, or kosh"
        add_link_moods "$2"

        shift
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
        line "Usage: install.sh [--dry-run] [--force] [--install-path DIR]"
        line "                  [--link-mood MOODS] [-x]"
        line "                  [--yes] [--uninstall]"
        line "  --dry-run           list the downloads, install nothing"
        line "  --force             install even when kosh is up to date"
        line "  --yes               accept installation or removal prompts"
        line "  --uninstall         remove the binary and its owned links"
        line "  --install-path DIR  install to DIR, extras to DIR/../share;"
        line "                      without it, the kosh on PATH is updated"
        line "  --link-mood MOODS   link bash, dash, or sh to kosh in DIR,"
        line "                      commas are accepted, existing files stay"
        line "  -x                  trace every command"
        line "  --help              print this help"
        line "Environment: KOSH_INSTALL_PATH, KOSH_INSTALL_VERSION,"
        line "  KOSH_INSTALL_DRY_RUN, KOSH_INSTALL_FORCE, NO_COLOR"
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
    "You can view the repository and this script at <github.com/fennec-support/kosh>"
  if [ -z "$BIN_DIR" ]
  then
    EXISTING=$(command -v kosh 2>/dev/null || :)
    case $EXISTING in
      /*) BIN_DIR=${EXISTING%/*} ;;
      *)
        BIN_DIR=$HOME/.local/bin
        if [ "$(id -u)" -eq 0 ]; then BIN_DIR=/usr/local/bin; fi
      ;;
    esac
  fi
  case $BIN_DIR in
    /*) ;;
    *) BIN_DIR=$PWD/$BIN_DIR ;;
  esac
  BIN_DIR=${BIN_DIR%/}
  SHARE_DIR=${BIN_DIR%/*}/share
  EXT=
  case $(uname -s) in MINGW* | MSYS* | CYGWIN*) EXT=.exe ;; esac

  if [ -n "$IS_UNINSTALL" ]
  then
    if [ -z "$IS_DRY_RUN" ] &&
      ! ask_strict "Do you want to remove Kosh from $BIN_DIR?"
    then
      line "Removal was cancelled. Pass --yes to remove without a terminal."
      exit 0
    fi
    for NAME in koshkit bash dash sh
    do
      if [ -L "$BIN_DIR/$NAME" ] && is_free_for_link "$BIN_DIR/$NAME"
      then
        if [ -n "$IS_DRY_RUN" ]; then
          line "Would remove $BIN_DIR/$NAME"
        else
          rm -f "$BIN_DIR/$NAME" || fail "cannot remove $BIN_DIR/$NAME"
        fi
      fi
    done
    if [ -e "$BIN_DIR/kosh$EXT" ] || [ -L "$BIN_DIR/kosh$EXT" ]
    then
      if [ -n "$IS_DRY_RUN" ]; then
        line "Would remove $BIN_DIR/kosh$EXT"
      else
        rm -f "$BIN_DIR/kosh$EXT" || fail "cannot remove $BIN_DIR/kosh$EXT"
        line "Removed $BIN_DIR/kosh$EXT"
      fi
    else
      line "No Kosh binary was found in $BIN_DIR."
    fi
    line "Configuration, history, completions, and manuals were kept."
    exit 0
  fi

  if command -v sha256sum >/dev/null; then
    CHECK="sha256sum -c"
  elif command -v shasum >/dev/null; then
    CHECK="shasum -a 256 -c"
  else
    fail "sha256sum or shasum is required to verify downloads"
  fi
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
  BINARY=kosh-$SYSTEM-$ARCH-$VERSION$EXT
  FILES="$BINARY kosh.bash"
  INSTALLED=$(installed_kosh)

  if [ -n "$INSTALLED" ] && [ -z "$IS_FORCE" ]
  then
    INSTALLED_VERSION=$(installed_version "$INSTALLED")

    if [ -n "$INSTALLED_VERSION" ]
    then
      if [ -n "${KOSH_INSTALL_VERSION:-}" ]
      then
        if [ "$INSTALLED_VERSION" = "$VERSION" ]
        then
          IS_CURRENT=1
        fi
      elif is_at_least "$INSTALLED_VERSION" "${VERSION#v}"
      then
        IS_CURRENT=1
      fi
    fi

    if [ -n "${IS_CURRENT:-}" ]
    then
      say "Kosh $INSTALLED_VERSION is already installed:" "$INSTALLED"
      line "You are up to date with $VERSION. Nothing to do."
      line "Pass --force to install it again."
      exit 0
    fi
  fi

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
    if [ -z "$EXT" ]
    then
      for NAME in koshkit $LINK_MOODS
      do
        if is_free_for_link "$BIN_DIR/$NAME"
        then
          line "Would link $BIN_DIR/$NAME to kosh"
        else
          line "Would keep $BIN_DIR/$NAME, which is not a link to kosh"
        fi
      done
    elif [ -n "$LINK_MOODS" ]
    then
      line "Would skip the mood links, which install.ps1 creates on Windows"
    fi
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
  if ! $CHECK expected >/dev/null 2>&1
  then
    printf 'not ok\n'
    fail "checksum mismatch"
  fi

  printf 'ok\n'
  mkdir -p "$BIN_DIR" || fail "cannot write to $BIN_DIR"
  [ -w "$BIN_DIR" ] ||
  fail "cannot write to $BIN_DIR; rerun as its owner or pass --install-path"
  # The new binary is renamed over the old one, so a running kosh keeps its
  # image and the path never holds a partial file.
  STAGED=$BIN_DIR/.kosh$EXT.$$
  install -m 755 "$BINARY" "$STAGED" || fail "cannot write to $BIN_DIR"
  if ! mv -f "$STAGED" "$BIN_DIR/kosh$EXT"
  then
    rm -f "$STAGED"
    fail "cannot replace $BIN_DIR/kosh$EXT"
  fi
  if [ -z "$EXT" ]
  then
    for NAME in koshkit $LINK_MOODS
    do
      if is_free_for_link "$BIN_DIR/$NAME"
      then
        ln -sf kosh "$BIN_DIR/$NAME" || fail "cannot link $BIN_DIR/$NAME"
      else
        line "Kept $BIN_DIR/$NAME, which is not a link to kosh"
      fi
    done
  elif [ -n "$LINK_MOODS" ]
  then
    line "Skipped the mood links, which install.ps1 creates on Windows"
  fi
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
      line "Install prefix is not in the PATH. Use $(paint "$IS_COLOR" '1;34' "$BIN_DIR/kosh$EXT") to launch the shell."
    ;;
  esac
}
