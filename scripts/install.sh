#!/bin/sh

#
#    This file is a part of the Koshka shell, (c) toiletbril, 2026
#    See the top-level LICENSE file for the licensing information.
#
# This file installs a release of the Koshka shell for the current system. It
# downloads the binary, the zstd-compressed kosh(1) and kosh(5) manual pages,
# and the Bash completion, verifies each against the SHA256SUMS file of the same
# release, and installs them under a prefix with the layout `make install` uses.
# It runs on Linux, macOS, and the MSYS2, Git Bash, and Cygwin environments on
# Windows.
#
# The script prints the detected system and processor and stops when the
# release has no build for them. It installs the latest release. KOSH_VERSION
# selects another release tag. KOSH_ARCH selects amd64 or aarch64 instead of the
# detected processor. KOSH_PREFIX selects the prefix and defaults to /usr/local
# for root and to ~/.local otherwise. The manual pages are skipped with a note
# when zstd is not installed.
#
#   curl -fsSL https://fennec.support/install-kosh | sh

set -eu
REPOSITORY=toiletbril/kosh
escape=$(printf '\033')
error_style=
bold_style=
note_style=
heading_style=
reset_style=

if [ -z "${NO_COLOR:-}" ] && [ "${TERM:-}" != dumb ]
then
  if [ -t 2 ]
  then
    error_style="$escape[1;91m"
    bold_style="$escape[1m"
    note_style="$escape[36m"
    reset_style="$escape[0m"
  fi

  if [ -t 1 ]
  then
    heading_style="$escape[1;34m"
  fi
fi

fail ()
{
  printf '%serror:%s %sinstall.sh: %s.%s\n' "$error_style" "$reset_style" \
    "$bold_style" "$1" "$reset_style" >&2
  exit 1
}

note ()
{
  printf '%snote:%s %s.\n' "$note_style" "$reset_style" "$1" >&2
}

report ()
{
  if [ -n "$heading_style" ]
  then
    printf '%s%s%s %s\n' "$heading_style" "$1" "$escape[0m" "$2"
  else
    printf '%s %s\n' "$1" "$2"
  fi
}

has_command ()
{
  command -v "$1" >/dev/null 2>&1
}

download ()
{
  if has_command curl
  then
    curl -fsSL -o "$2" "$1"
  elif has_command wget
  then
    wget -q -O "$2" "$1"
  else
    fail "curl or wget is required"
  fi
}

file_sha256 ()
{
  if has_command sha256sum
  then
    sha256sum "$1" | cut -d ' ' -f 1
  elif has_command shasum
  then
    shasum -a 256 "$1" | cut -d ' ' -f 1
  else
    fail "sha256sum or shasum is required to verify the download"
  fi
}

listed_sha256 ()
{
  awk \
      -v name="$1" '$2 == name || $2 == "*" name { print $1 }' \
    "$work_dir/SHA256SUMS"
}

fetch_verified ()
{
  download "$base_url/$1" "$work_dir/$1" ||
  fail "unable to download $base_url/$1"
  [ -n "$(listed_sha256 "$1")" ] || fail "SHA256SUMS does not list $1"
  [ "$(file_sha256 "$work_dir/$1")" = "$(listed_sha256 "$1")" ] ||
  fail "checksum mismatch for $1"
}

latest_release_url ()
{
  if has_command curl
  then
    curl -fsSLI -o /dev/null -w '%{url_effective}' "$1"
  elif has_command wget
  then
    wget -q -S --spider "$1" 2>&1 |
    sed -n 's/^ *[Ll]ocation: *//p' | tail -n 1 | tr -d '\r'
  else
    fail "curl or wget is required"
  fi
}

latest_release_version ()
{
  latest_release_url "https://github.com/$REPOSITORY/releases/latest" |
  sed 's|.*/||'
}

place_file ()
{
  mkdir -p "$(dirname "$3")"
  cp "$1" "$3.new"
  chmod "$2" "$3.new"
  mv -f "$3.new" "$3"
}

suffix=
platform=
processor=

case $(uname -s) in
  Linux)
    platform=linux
  ;;
  Darwin)
    platform=darwin
  ;;
  MINGW* | MSYS* | CYGWIN*)
    platform=win32
    suffix=.exe
  ;;
  *)
    fail "unsupported system '$(uname -s)'"
  ;;
esac

machine=${KOSH_ARCH:-$(uname -m)}

case $machine in
  x86_64 | amd64)
    processor=amd64
  ;;
  aarch64 | arm64)
    processor=aarch64
  ;;
  *)
    fail "unsupported processor '$machine'"
  ;;
esac

report Detected "$(uname -s) on $processor"
if [ -n "${KOSH_VERSION:-}" ]
then
  version=$KOSH_VERSION
else
  version=$(latest_release_version)

  case $version in
    '' | latest | releases)
      fail "unable to resolve the latest release"
    ;;
    *)
    ;;
  esac
fi

if [ -n "${KOSH_PREFIX:-}" ]
then
  prefix=$KOSH_PREFIX
elif [ "$(id -u)" -eq 0 ]
then
  prefix=/usr/local
else
  prefix=$HOME/.local
fi

asset=kosh-$platform-$processor-$version$suffix
base_url=https://github.com/$REPOSITORY/releases/download/$version
work_dir=$(mktemp -d)

trap 'rm -rf "$work_dir"' EXIT INT TERM
report Installing "kosh $version into $prefix"
download "$base_url/SHA256SUMS" "$work_dir/SHA256SUMS" ||
fail "release $version has no SHA256SUMS file"
[ -n "$(listed_sha256 "$asset")" ] ||
fail "release $version has no build for $platform on $processor"
fetch_verified "$asset"
fetch_verified kosh.bash
place_file "$work_dir/$asset" 755 "$prefix/bin/kosh$suffix"
place_file "$work_dir/kosh.bash" 644 \
  "$prefix/share/bash-completion/completions/kosh"
if has_command zstd
then
  for page in kosh.1 kosh.5
  do
    fetch_verified "$page.zst"
    zstd -dqf "$work_dir/$page.zst" -o "$work_dir/$page"
    place_file "$work_dir/$page" 644 "$prefix/share/man/man${page##*.}/$page"
  done
else
  note "zstd is not installed, so the manual pages were skipped"
fi

report Installed "$prefix/bin/kosh$suffix"
case ":$PATH:" in
  *":$prefix/bin:"*)
  ;;
  *)
    note "add $prefix/bin to PATH to run kosh by name"
  ;;
esac
