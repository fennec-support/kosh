#!/bin/sh
set -eu
ASSIMILATE_SIDE=$1
shift
[ "${5-0}" -eq 0 ] || set -x
umask 077
UPLOAD=$1
LINK_MOODS=${4-}
CANDIDATE=
TARGET=
LOCK=
BACKUP=
LOCK_OWNED=0
LOCK_PUBLISHED=0
HAD_TARGET=0
ROLLBACK_ACTIVE=0
COMMITTED=0
INSTALL_DIR=
INSTALL_REJECTION=
REMAINING_MOODS=
LINK_NAME=
LINK_PATH=
LOCK_PATH=
LOCK_RECORD=
STALE_LOCK=
STALE_UPLOAD=
STALE_CANDIDATE=
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
remove_recorded_links() {
  LINKS_FILE=$1
  [ ! -r "$LINKS_FILE" ] || while IFS= read -r LINK_NAME; do
    case $LINK_NAME in bash|dash|sh) ;; *) return 1 ;; esac
    LINK_PATH=${LINKS_FILE%/*}/../$LINK_NAME
    [ ! -L "$LINK_PATH" ] || koshkit rm -f "$LINK_PATH" || return 1
  done < "$LINKS_FILE"
}
cleanup() {
  TRANSACTION_STATUS=$?
  [ "$#" -eq 0 ] || TRANSACTION_STATUS=$1
  if [ "$ROLLBACK_ACTIVE" -eq 1 ] && [ "$COMMITTED" -eq 0 ] &&
     [ "$TRANSACTION_STATUS" -eq 0 ]; then
    TRANSACTION_STATUS=1
  fi
  CLEANUP_FAILED=0
  trap - EXIT HUP INT TERM
  if [ "$LOCK_OWNED" -eq 1 ]; then
    [ -n "$CANDIDATE" ] || [ ! -r "$LOCK/candidate" ] || CANDIDATE=$(koshkit cat "$LOCK/candidate")
    [ -n "$UPLOAD" ] || [ ! -r "$LOCK/upload" ] || UPLOAD=$(koshkit cat "$LOCK/upload")
    if [ "$ROLLBACK_ACTIVE" -eq 1 ] && [ "$COMMITTED" -eq 0 ]; then
      remove_recorded_links "$LOCK/links" || CLEANUP_FAILED=1
      if [ "$HAD_TARGET" -eq 1 ] && { [ -e "$BACKUP" ] || [ -L "$BACKUP" ]; }; then
        koshkit mv -f "$BACKUP" "$TARGET" || CLEANUP_FAILED=1
      elif [ "$HAD_TARGET" -eq 1 ]; then
        [ -e "$TARGET" ] || [ -L "$TARGET" ] || CLEANUP_FAILED=1
      else
        koshkit rm -f "$TARGET" || CLEANUP_FAILED=1
      fi
    fi
    [ -z "$CANDIDATE" ] || koshkit rm -f "$CANDIDATE" || CLEANUP_FAILED=1
    [ -z "$UPLOAD" ] || koshkit rm -f "$UPLOAD" || CLEANUP_FAILED=1
    if [ "$COMMITTED" -eq 1 ] || [ "$ROLLBACK_ACTIVE" -eq 0 ]; then
      [ -z "$BACKUP" ] || koshkit rm -f "$BACKUP" || CLEANUP_FAILED=1
    fi
    if [ "$CLEANUP_FAILED" -eq 0 ]; then
      koshkit rm -f "$LOCK/upload" "$LOCK/candidate" \
        "$LOCK/had-target" "$LOCK/rollback" "$LOCK/committed" \
        "$LOCK/links" || CLEANUP_FAILED=1
      [ "$LOCK_PUBLISHED" -eq 0 ] || koshkit rm -f "$LOCK_PATH" || CLEANUP_FAILED=1
      if [ "$CLEANUP_FAILED" -eq 0 ]; then
        koshkit rm -f "$LOCK/owner" || CLEANUP_FAILED=1
        [ "$CLEANUP_FAILED" -ne 0 ] || koshkit rmdir "$LOCK" || CLEANUP_FAILED=1
      fi
    fi
  else
    [ -z "$CANDIDATE" ] || koshkit rm -f "$CANDIDATE" || CLEANUP_FAILED=1
    [ -z "$UPLOAD" ] || koshkit rm -f "$UPLOAD" || CLEANUP_FAILED=1
  fi
  [ "$CLEANUP_FAILED" -eq 0 ] || exit 1
  exit "$TRANSACTION_STATUS"
}
load_stale_lock_record() {
  STALE_OWNER=${LOCK_RECORD%%:*}
  STALE_LOCK_NAME=${LOCK_RECORD#*:}
  [ "$STALE_LOCK_NAME" != "$LOCK_RECORD" ] || return 1
  case $STALE_OWNER in ''|*[!0-9]*) return 1 ;; *) ;; esac
  case $STALE_LOCK_NAME in .kosh-assimilate-*.lock) ;; *) return 1 ;; esac
  STALE_TRANSACTION_ID=${STALE_LOCK_NAME#.kosh-assimilate-}
  STALE_TRANSACTION_ID=${STALE_TRANSACTION_ID%.lock}
  STALE_PROCESS_ID=${STALE_TRANSACTION_ID%%-*}
  STALE_TIMESTAMP=${STALE_TRANSACTION_ID#*-}
  [ "$STALE_TIMESTAMP" != "$STALE_TRANSACTION_ID" ] || return 1
  case $STALE_PROCESS_ID in ''|*[!0-9]*) return 1 ;; *) ;; esac
  case $STALE_TIMESTAMP in ''|*[!0-9]*) return 1 ;; *) ;; esac
  [ "$STALE_TRANSACTION_ID" = "$STALE_PROCESS_ID-$STALE_TIMESTAMP" ] || return 1
  STALE_LOCK=$INSTALL_DIR/$STALE_LOCK_NAME
  [ -d "$STALE_LOCK" ] && [ ! -L "$STALE_LOCK" ] || return 1
  STALE_UPLOAD=.kosh-assimilate-$STALE_TRANSACTION_ID.upload
  STALE_CANDIDATE=$INSTALL_DIR/.kosh-assimilate-$STALE_TRANSACTION_ID.candidate
  [ "${1-0}" -eq 0 ] || return 0
  [ ! -L "$STALE_LOCK/upload" ] || return 1
  [ ! -L "$STALE_LOCK/candidate" ] || return 1
  if [ -e "$STALE_LOCK/upload" ]; then
    RECORDED_UPLOAD=$(koshkit cat "$STALE_LOCK/upload") || return 1
    [ "$RECORDED_UPLOAD" = "$STALE_UPLOAD" ] || return 1
  fi
  if [ -e "$STALE_LOCK/candidate" ]; then
    RECORDED_CANDIDATE=$(koshkit cat "$STALE_LOCK/candidate") || return 1
    [ "$RECORDED_CANDIDATE" = "$STALE_CANDIDATE" ] || return 1
  fi
}
recover_stale_lock() {
  STALE_BACKUP=$STALE_LOCK/backup
  if [ ! -e "$STALE_LOCK/committed" ] && [ -e "$STALE_LOCK/rollback" ]; then
    remove_recorded_links "$STALE_LOCK/links" || return 1
    if [ -e "$STALE_LOCK/had-target" ]; then
      if [ -e "$STALE_BACKUP" ] || [ -L "$STALE_BACKUP" ]; then
        koshkit mv -f "$STALE_BACKUP" "$TARGET" || return 1
      else
        [ -e "$TARGET" ] || [ -L "$TARGET" ] || return 1
      fi
    else
      koshkit rm -f "$TARGET" || return 1
    fi
  fi
  koshkit rm -f "$STALE_CANDIDATE" "$STALE_UPLOAD" "$STALE_BACKUP" || return 1
  koshkit rm -f "$STALE_LOCK/upload" "$STALE_LOCK/candidate" \
    "$STALE_LOCK/had-target" "$STALE_LOCK/rollback" \
    "$STALE_LOCK/committed" "$STALE_LOCK/links" || return 1
  if [ "${1-0}" -eq 1 ]; then
    CURRENT_LOCK_RECORD=$(koshkit cat "$LOCK_PATH") || return 1
    [ "$CURRENT_LOCK_RECORD" = "$LOCK_RECORD" ] || return 1
    koshkit rm -f "$LOCK_PATH" || return 1
  fi
  koshkit rm -f "$STALE_LOCK/owner" || return 1
  koshkit rmdir "$STALE_LOCK" || return 1
}
recover_existing_lock() {
  LOCK_RECORD=$(koshkit cat "$LOCK_PATH") || return 1
  load_stale_lock_record || return 1
  recover_stale_lock 1
}
recover_orphan_locks() {
  for ORPHAN_LOCK in "$INSTALL_DIR"/.kosh-assimilate-*.lock; do
    [ -d "$ORPHAN_LOCK" ] || continue
    [ ! -L "$ORPHAN_LOCK" ] || return 1
    if [ ! -e "$ORPHAN_LOCK/rollback" ] && [ ! -e "$ORPHAN_LOCK/committed" ]; then
      LOCK_RECORD=0:${ORPHAN_LOCK##*/}
      load_stale_lock_record 1 || return 1
    else
      LOCK_RECORD=$(koshkit cat "$ORPHAN_LOCK/owner") || return 1
      load_stale_lock_record || return 1
    fi
    [ "$STALE_LOCK" = "$ORPHAN_LOCK" ] || return 1
    recover_stale_lock 0 || return 1
  done
}
recover_all_stale_locks() {
  if [ -e "$LOCK_PATH" ] || [ -L "$LOCK_PATH" ]; then
    recover_existing_lock || return 1
  fi
  recover_orphan_locks
}
try_install_directory() {
  directory_is_install_candidate "$1" || return 1
  LOCK_PATH=$INSTALL_DIR/.kosh-assimilate.lock
  if ! recover_all_stale_locks; then
    INSTALL_REJECTION="the lock '$LOCK_PATH' belongs to another install or cannot be recovered"
    return 1
  fi
  directory_accepts_links || return 1
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
if ! try_install_directory "${6-}"; then
  printf 'assimilate: Cannot install into %s: %s\n' "${6-}" "$INSTALL_REJECTION" >&2
  exit 3
fi
CANDIDATE=$INSTALL_DIR/.kosh-assimilate-$2.candidate
[ ! -d "$TARGET" ] || exit 1
recover_all_stale_locks
trap '' HUP INT TERM
NEW_LOCK=$INSTALL_DIR/.kosh-assimilate-$2.lock
koshkit mkdir "$NEW_LOCK"
LOCK=$NEW_LOCK
LOCK_OWNED=1
BACKUP=$LOCK/backup
printf '%s:%s\n' "$$" "${LOCK##*/}" > "$LOCK/owner"
printf '%s\n' "$UPLOAD" > "$LOCK/upload"
printf '%s\n' "$CANDIDATE" > "$LOCK/candidate"
: > "$LOCK/links"
if ! koshkit ln -s "$LOCK/owner" "$LOCK_PATH"; then
  recover_existing_lock
  koshkit ln -s "$LOCK/owner" "$LOCK_PATH"
fi
LOCK_PUBLISHED=1
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
koshkit mv "$UPLOAD" "$CANDIDATE"
if ! CANDIDATE_IDENTITY=$("$CANDIDATE" -p --mood sh -c 'printf "%s\n" "$KOSH_IDENTITY"'); then
  cleanup 1
fi
[ -n "$CANDIDATE_IDENTITY" ] && [ "$CANDIDATE_IDENTITY" = "$3" ] || cleanup 1
if [ -e "$TARGET" ] || [ -L "$TARGET" ]; then
  HAD_TARGET=1
  : > "$LOCK/had-target"
fi
ROLLBACK_ACTIVE=1
: > "$LOCK/rollback"
[ "$HAD_TARGET" -eq 0 ] || koshkit mv -f "$TARGET" "$BACKUP"
koshkit mv -f "$CANDIDATE" "$TARGET"
CANDIDATE=
if ! TARGET_IDENTITY=$("$TARGET" -p --mood sh -c 'printf "%s\n" "$KOSH_IDENTITY"'); then
  cleanup 1
fi
[ -n "$TARGET_IDENTITY" ] && [ "$TARGET_IDENTITY" = "$CANDIDATE_IDENTITY" ] || cleanup 1
REMAINING_MOODS=$LINK_MOODS
while [ -n "$REMAINING_MOODS" ]; do
  case $REMAINING_MOODS in
    *,*) LINK_NAME=${REMAINING_MOODS%%,*}; REMAINING_MOODS=${REMAINING_MOODS#*,} ;;
    *) LINK_NAME=$REMAINING_MOODS; REMAINING_MOODS= ;;
  esac
  [ "$LINK_NAME" = kosh ] && continue
  LINK_PATH=$INSTALL_DIR/$LINK_NAME
  if [ -e "$LINK_PATH" ] || [ -L "$LINK_PATH" ]; then
    # shellcheck disable=SC3013
    [ -L "$LINK_PATH" ] && [ "$LINK_PATH" -ef "$TARGET" ] || cleanup 1
  else
    koshkit ln -s kosh "$LINK_PATH" || cleanup 1
    printf '%s\n' "$LINK_NAME" >> "$LOCK/links"
  fi
done
trap '' HUP INT TERM
if ! : > "$LOCK/committed"; then
  cleanup 1
  exit 1
fi
COMMITTED=1
ROLLBACK_ACTIVE=0
koshkit rm -f "$LOCK/rollback"
[ "$HAD_TARGET" -eq 0 ] || koshkit rm -f "$BACKUP"
koshkit rm -f "$LOCK/upload" "$LOCK/candidate" \
  "$LOCK/had-target" "$LOCK/committed" "$LOCK/links"
koshkit rm -f "$LOCK_PATH"
LOCK_PUBLISHED=0
koshkit rm -f "$LOCK/owner"
koshkit rmdir "$LOCK"
LOCK_OWNED=0
LOCK=
koshkit rm -f "$UPLOAD"
trap - EXIT HUP INT TERM
printf '%s\n' "$TARGET"
