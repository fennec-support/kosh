#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#

# Run:
#     curl -fsSL "https://fennec.support/kosh/install" | sh

{
  set -eu
  releases=https://github.com/toiletbril/kosh/releases
  red=
  blue=
  reset=
  os=
  arch=
  ext=

  if [ -t 2 ] && [ -z "${NO_COLOR:-}" ]
  then
    red=$(printf '\033[1;91m')
    blue=$(printf '\033[1;34m')
    reset=$(printf '\033[0m')
  fi

  fail ()
  {
    printf '%serror:%s install.sh: %s.\n' "$red" "$reset" "$1" >&2
    exit 1
  }

  fetch ()
  {
    curl --proto '=https' --tlsv1.2 --retry 3 -fsSL "$@"
  }

  command -v curl >/dev/null || fail "curl is required"
  case $(uname -s) in
    Linux)
      os=linux ext=
    ;;
    Darwin)
      os=darwin ext=
    ;;
    MINGW* | MSYS* | CYGWIN*)
      os=win32 ext=.exe
    ;;
    *)
      fail "unsupported system $(uname -s)"
    ;;
  esac

  case $(uname -m) in
    x86_64 | amd64)
      arch=amd64
    ;;
    aarch64 | arm64)
      arch=aarch64
    ;;
    *)
      fail "unsupported processor $(uname -m)"
    ;;
  esac

  if [ "$(sysctl -n sysctl.proc_translated 2>/dev/null || :)" = 1 ]
  then
    arch=aarch64
  fi

  version=${KOSH_INSTALL_VERSION:-}

  if [ -z "$version" ]
  then
    latest=$(fetch -o /dev/null -w '%{url_effective}' "$releases/latest")
    version=${latest##*/}
  fi

  case $version in
    '' | latest | releases | *[!A-Za-z0-9._-]*)
      fail "invalid release version '$version'"
    ;;
    *)
    ;;
  esac

  prefix=${KOSH_INSTALL_PREFIX:-$HOME/.local}

  if [ -z "${KOSH_INSTALL_PREFIX:-}" ] && [ "$(id -u)" -eq 0 ]
  then
    prefix=/usr/local
  fi

  binary=kosh-$os-$arch-$version$ext
  files="$binary kosh.bash"

  if command -v zstd >/dev/null
  then
    files="$files kosh.1.zst kosh.5.zst"
  fi

  printf '%sInstalling%s kosh %s for %s on %s into %s\n' "$blue" "$reset" \
    "$version" "$os" "$arch" "$prefix"
  work=$(mktemp -d)

  trap 'rm -rf "$work"' EXIT
  trap 'exit 130' INT TERM
  cd "$work" || fail "cannot enter $work"
  url=$releases/download/$version

  fetch -o SHA256SUMS "$url/SHA256SUMS" ||
  fail "release $version has no SHA256SUMS file"
  grep -q " \*\{0,1\}$binary\$" SHA256SUMS ||
  fail "release $version has no build for $os on $arch"
  for file in $files
  do
    fetch -o "$file" "$url/$file" || fail "unable to download $file"
    grep " \*\{0,1\}$file\$" SHA256SUMS >>expected
  done

  if command -v sha256sum >/dev/null
  then
    sha256sum -c expected >/dev/null || fail "checksum mismatch"
  else
    shasum -a 256 -c expected >/dev/null || fail "checksum mismatch"
  fi

  mkdir -p "$prefix/bin" "$prefix/share/bash-completion/completions" ||
  fail "cannot write to $prefix"
  install -m 755 "$binary" "$prefix/bin/kosh$ext"
  install -m 644 kosh.bash "$prefix/share/bash-completion/completions/kosh"
  if [ -f kosh.1.zst ]
  then
    mkdir -p "$prefix/share/man/man1" "$prefix/share/man/man5"
    zstd -dq kosh.1.zst kosh.5.zst
    install -m 644 kosh.1 "$prefix/share/man/man1/kosh.1"
    install -m 644 kosh.5 "$prefix/share/man/man5/kosh.5"
  fi

  printf '%sInstalled%s %s\n' "$blue" "$reset" "$prefix/bin/kosh$ext"
  case ":$PATH:" in
    *":$prefix/bin:"*)
    ;;
    *)
      printf 'Add %s to PATH to run kosh by name.\n' "$prefix/bin"
    ;;
  esac

  exit 0
}
