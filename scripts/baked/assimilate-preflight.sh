#!/bin/sh
set -eu
ASSIMILATE_SIDE=$1
shift
[ "$1" -eq 0 ] || set -x
EXPECTED_SYSTEM=$2
EXPECTED_MACHINE=$3
REMOTE_SYSTEM=any
REMOTE_MACHINE=any
if [ "$EXPECTED_SYSTEM" != any ] && ! REMOTE_SYSTEM=$(uname -s 2>/dev/null); then
  printf 'assimilate-error=Cannot determine the %s operating system\n' "$ASSIMILATE_SIDE"
  exit 125
fi
if [ "$EXPECTED_MACHINE" != any ] && ! REMOTE_MACHINE=$(uname -m 2>/dev/null); then
  printf 'assimilate-error=Cannot determine the %s architecture\n' "$ASSIMILATE_SIDE"
  exit 125
fi
case $EXPECTED_MACHINE in
  arm64|aarch64) EXPECTED_MACHINE=arm64 ;;
  x86_64|amd64) EXPECTED_MACHINE=x86_64 ;;
  *) ;;
esac
case $REMOTE_MACHINE in
  arm64|aarch64) REMOTE_MACHINE=arm64 ;;
  x86_64|amd64) REMOTE_MACHINE=x86_64 ;;
  *) ;;
esac
SYSTEM_MISMATCH=0
MACHINE_MISMATCH=0
[ "$EXPECTED_SYSTEM" = any ] || [ "$EXPECTED_SYSTEM" = "$REMOTE_SYSTEM" ] || SYSTEM_MISMATCH=1
[ "$EXPECTED_MACHINE" = any ] || [ "$EXPECTED_MACHINE" = "$REMOTE_MACHINE" ] || MACHINE_MISMATCH=1
if [ "$SYSTEM_MISMATCH" -ne 0 ] || [ "$MACHINE_MISMATCH" -ne 0 ]; then
  printf 'assimilate-error=Cannot install a %s/%s binary on %s/%s\n' \
    "$EXPECTED_SYSTEM" "$EXPECTED_MACHINE" "$REMOTE_SYSTEM" "$REMOTE_MACHINE"
  exit 126
fi
REQUESTED_DIRECTORY=${4-}
LINK_MOODS=${5-}
INSTALL_REJECTION=
INSTALL_DIR=
TARGET=
REMAINING_PATH=
PATH_ENTRY=
HAS_MORE=0
directory_is_install_candidate() {
  PROPOSED_DIRECTORY=$1
  if [ -z "$PROPOSED_DIRECTORY" ]; then
    INSTALL_REJECTION='the directory path is empty'
    return 1
  fi
  if [ ! -e "$PROPOSED_DIRECTORY" ] && [ ! -L "$PROPOSED_DIRECTORY" ]; then
    INSTALL_REJECTION="'$PROPOSED_DIRECTORY' does not exist on the $ASSIMILATE_SIDE host"
    return 1
  fi
  if [ ! -d "$PROPOSED_DIRECTORY" ]; then
    INSTALL_REJECTION="'$PROPOSED_DIRECTORY' is not a directory"
    return 1
  fi
  if [ ! -w "$PROPOSED_DIRECTORY" ]; then
    REMOTE_USER=$(id -un 2>/dev/null) || REMOTE_USER=unknown
    INSTALL_REJECTION="the $ASSIMILATE_SIDE user '$REMOTE_USER' cannot write to '$PROPOSED_DIRECTORY'"
    return 1
  fi
  if [ ! -x "$PROPOSED_DIRECTORY" ]; then
    REMOTE_USER=$(id -un 2>/dev/null) || REMOTE_USER=unknown
    INSTALL_REJECTION="the $ASSIMILATE_SIDE user '$REMOTE_USER' cannot search '$PROPOSED_DIRECTORY' because it lacks execute permission"
    return 1
  fi
  if ! INSTALL_DIR=$(cd "$PROPOSED_DIRECTORY" && pwd -P); then
    INSTALL_REJECTION="'$PROPOSED_DIRECTORY' cannot be resolved to a physical path"
    return 1
  fi
  TARGET=$INSTALL_DIR/kosh
  if [ -d "$TARGET" ]; then
    INSTALL_REJECTION="'$TARGET' is a directory, so the kosh binary cannot be placed there"
    return 1
  fi
}
directory_accepts_links() {
  REMAINING_MOODS=$LINK_MOODS
  while [ -n "$REMAINING_MOODS" ]; do
    case $REMAINING_MOODS in
      *,*) LINK_NAME=${REMAINING_MOODS%%,*}; REMAINING_MOODS=${REMAINING_MOODS#*,} ;;
      *) LINK_NAME=$REMAINING_MOODS; REMAINING_MOODS= ;;
    esac
    [ "$LINK_NAME" = kosh ] && continue
    LINK_PATH=$INSTALL_DIR/$LINK_NAME
    if [ -L "$LINK_PATH" ]; then
      # shellcheck disable=SC3013
      if ! [ "$LINK_PATH" -ef "$TARGET" ]; then
        INSTALL_REJECTION="'$LINK_PATH' is a symbolic link that does not point to '$TARGET', and assimilate does not replace it"
        return 1
      fi
    elif [ -e "$LINK_PATH" ]; then
      INSTALL_REJECTION="'$LINK_PATH' already exists and is not a symbolic link to '$TARGET', and assimilate does not replace it"
      return 1
    fi
  done
}
path_contains_directory() {
  WANTED_DIRECTORY=$1
  REMAINING_PATH=${PATH-}
  while :; do
    case $REMAINING_PATH in
      *:*) PATH_ENTRY=${REMAINING_PATH%%:*}; REMAINING_PATH=${REMAINING_PATH#*:}; HAS_MORE=1 ;;
      *) PATH_ENTRY=$REMAINING_PATH; HAS_MORE=0 ;;
    esac
    [ -n "$PATH_ENTRY" ] || PATH_ENTRY=.
    [ "$PATH_ENTRY" != "$WANTED_DIRECTORY" ] || return 0
    [ "$HAS_MORE" -eq 1 ] || return 1
  done
}
directory_is_usable() {
  directory_is_install_candidate "$1" && directory_accepts_links
}
select_install_directory() {
  if [ -n "$REQUESTED_DIRECTORY" ]; then
    directory_is_usable "$REQUESTED_DIRECTORY" && return 0
    printf 'assimilate-error=Cannot install into %s: %s\n' \
      "$REQUESTED_DIRECTORY" "$INSTALL_REJECTION"
    return 1
  fi
  if path_contains_directory /usr/local/bin &&
     directory_is_usable /usr/local/bin; then
    return 0
  fi
  if [ -n "${HOME-}" ] && path_contains_directory "$HOME/.local/bin" &&
     directory_is_usable "$HOME/.local/bin"; then
    return 0
  fi
  REMAINING_PATH=${PATH-}
  while :; do
    case $REMAINING_PATH in
      *:*) PATH_ENTRY=${REMAINING_PATH%%:*}; REMAINING_PATH=${REMAINING_PATH#*:}; HAS_MORE=1 ;;
      *) PATH_ENTRY=$REMAINING_PATH; HAS_MORE=0 ;;
    esac
    [ -n "$PATH_ENTRY" ] || PATH_ENTRY=.
    case $PATH_ENTRY in
      /usr/local/bin|*sbin*) ;;
      "${HOME-}/.local/bin") ;;
      *) directory_is_usable "$PATH_ENTRY" && return 0 ;;
    esac
    [ "$HAS_MORE" -eq 1 ] || break
  done
  printf 'assimilate-error=The %s PATH has no writable directory for kosh\n' "$ASSIMILATE_SIDE"
  return 1
}
select_install_directory || exit 1
printf 'assimilate-install-directory=%s\n' "$INSTALL_DIR"
