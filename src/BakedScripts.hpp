/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file holds the shell scripts built into Koshka: the default user rc
 * script, the assimilate install scripts, the flock transaction keeper, and
 * the goodcore debugger launchers.
 */

#pragma once

#include "base/StringView.hpp"

namespace koshka {

/* the default user rc script. A login shell sources the Bash login files, and
 * an interactive shell sources the POSIX and Bash rc files. */
constexpr StringView DEFAULT_RC_SCRIPT{
    R"(#!/usr/bin/env kosh
if shopt -q login_shell; then
  [ -r /etc/profile ] && . /etc/profile
  if [ -r ~/.bash_profile ]; then
    . ~/.bash_profile
  elif [ -r ~/.bash_login ]; then
    . ~/.bash_login
  elif [ -r ~/.profile ]; then
    . ~/.profile
  fi
else
  [ -r ~/.profile ] && . ~/.profile
  [ -r ~/.bashrc ] && . ~/.bashrc
fi
)"};

inline constexpr char ASSIMILATE_DIRECTORY_FUNCTIONS_TEXT[] =
    R"SH(install_rejection=
directory_is_install_candidate() {
  proposed_directory=$1
  if [ -z "$proposed_directory" ]; then
    install_rejection='the directory path is empty'
    return 1
  fi
  if [ ! -e "$proposed_directory" ] && [ ! -L "$proposed_directory" ]; then
    install_rejection="'$proposed_directory' does not exist on the $assimilate_side host"
    return 1
  fi
  if [ ! -d "$proposed_directory" ]; then
    install_rejection="'$proposed_directory' is not a directory"
    return 1
  fi
  if [ ! -w "$proposed_directory" ]; then
    remote_user=$(id -un 2>/dev/null) || remote_user=unknown
    install_rejection="the $assimilate_side user '$remote_user' cannot write to '$proposed_directory'"
    return 1
  fi
  if [ ! -x "$proposed_directory" ]; then
    remote_user=$(id -un 2>/dev/null) || remote_user=unknown
    install_rejection="the $assimilate_side user '$remote_user' cannot search '$proposed_directory' because it lacks execute permission"
    return 1
  fi
  if ! install_dir=$(cd "$proposed_directory" && pwd -P); then
    install_rejection="'$proposed_directory' cannot be resolved to a physical path"
    return 1
  fi
  target=$install_dir/kosh
  if [ -d "$target" ]; then
    install_rejection="'$target' is a directory, so the kosh binary cannot be placed there"
    return 1
  fi
}
directory_accepts_links() {
  remaining_moods=$link_moods
  while [ -n "$remaining_moods" ]; do
    case $remaining_moods in
      *,*) link_name=${remaining_moods%%,*}; remaining_moods=${remaining_moods#*,} ;;
      *) link_name=$remaining_moods; remaining_moods= ;;
    esac
    [ "$link_name" = kosh ] && continue
    link_path=$install_dir/$link_name
    if [ -L "$link_path" ]; then
      if ! [ "$link_path" -ef "$target" ]; then
        install_rejection="'$link_path' is a symbolic link that does not point to '$target', and assimilate does not replace it"
        return 1
      fi
    elif [ -e "$link_path" ]; then
      install_rejection="'$link_path' already exists and is not a symbolic link to '$target', and assimilate does not replace it"
      return 1
    fi
  done
}
)SH";
inline constexpr StringView ASSIMILATE_DIRECTORY_FUNCTIONS{
    ASSIMILATE_DIRECTORY_FUNCTIONS_TEXT,
    sizeof(ASSIMILATE_DIRECTORY_FUNCTIONS_TEXT) - 1};

inline constexpr char       ASSIMILATE_TRANSACTION_TEXT[] = R"SH(set -eu
[ "${5-0}" -eq 0 ] || set -x
umask 077
upload=$1
link_moods=${4-}
candidate=
target=
lock=
backup=
lock_owned=0
lock_published=0
had_target=0
rollback_active=0
committed=0
remove_recorded_links() {
  links_file=$1
  [ ! -r "$links_file" ] || while IFS= read -r link_name; do
    case $link_name in bash|dash|sh) ;; *) return 1 ;; esac
    link_path=${links_file%/*}/../$link_name
    [ ! -L "$link_path" ] || koshkit rm -f "$link_path" || return 1
  done < "$links_file"
}
cleanup() {
  transaction_status=$?
  [ "$#" -eq 0 ] || transaction_status=$1
  if [ "$rollback_active" -eq 1 ] && [ "$committed" -eq 0 ] &&
     [ "$transaction_status" -eq 0 ]; then
    transaction_status=1
  fi
  cleanup_failed=0
  trap - 0 1 2 15
  if [ "$lock_owned" -eq 1 ]; then
    [ -n "$candidate" ] || [ ! -r "$lock/candidate" ] || candidate=$(koshkit cat "$lock/candidate")
    [ -n "$upload" ] || [ ! -r "$lock/upload" ] || upload=$(koshkit cat "$lock/upload")
    if [ "$rollback_active" -eq 1 ] && [ "$committed" -eq 0 ]; then
      remove_recorded_links "$lock/links" || cleanup_failed=1
      if [ "$had_target" -eq 1 ] && { [ -e "$backup" ] || [ -L "$backup" ]; }; then
        koshkit mv -f "$backup" "$target" || cleanup_failed=1
      elif [ "$had_target" -eq 1 ]; then
        [ -e "$target" ] || [ -L "$target" ] || cleanup_failed=1
      else
        koshkit rm -f "$target" || cleanup_failed=1
      fi
    fi
    [ -z "$candidate" ] || koshkit rm -f "$candidate" || cleanup_failed=1
    [ -z "$upload" ] || koshkit rm -f "$upload" || cleanup_failed=1
    if [ "$committed" -eq 1 ] || [ "$rollback_active" -eq 0 ]; then
      [ -z "$backup" ] || koshkit rm -f "$backup" || cleanup_failed=1
    fi
    if [ "$cleanup_failed" -eq 0 ]; then
      koshkit rm -f "$lock/upload" "$lock/candidate" \
        "$lock/had-target" "$lock/rollback" "$lock/committed" \
        "$lock/links" || cleanup_failed=1
      [ "$lock_published" -eq 0 ] || koshkit rm -f "$lock_path" || cleanup_failed=1
      if [ "$cleanup_failed" -eq 0 ]; then
        koshkit rm -f "$lock/owner" || cleanup_failed=1
        [ "$cleanup_failed" -ne 0 ] || koshkit rmdir "$lock" || cleanup_failed=1
      fi
    fi
  else
    [ -z "$candidate" ] || koshkit rm -f "$candidate" || cleanup_failed=1
    [ -z "$upload" ] || koshkit rm -f "$upload" || cleanup_failed=1
  fi
  [ "$cleanup_failed" -eq 0 ] || exit 1
  exit "$transaction_status"
}
load_stale_lock_record() {
  stale_owner=${lock_record%%:*}
  stale_lock_name=${lock_record#*:}
  [ "$stale_lock_name" != "$lock_record" ] || return 1
  case $stale_owner in ''|*[!0-9]*) return 1 ;; esac
  case $stale_lock_name in .kosh-assimilate-*.lock) ;; *) return 1 ;; esac
  stale_transaction_id=${stale_lock_name#.kosh-assimilate-}
  stale_transaction_id=${stale_transaction_id%.lock}
  stale_process_id=${stale_transaction_id%%-*}
  stale_timestamp=${stale_transaction_id#*-}
  [ "$stale_timestamp" != "$stale_transaction_id" ] || return 1
  case $stale_process_id in ''|*[!0-9]*) return 1 ;; esac
  case $stale_timestamp in ''|*[!0-9]*) return 1 ;; esac
  [ "$stale_transaction_id" = "$stale_process_id-$stale_timestamp" ] || return 1
  stale_lock=$install_dir/$stale_lock_name
  [ -d "$stale_lock" ] && [ ! -L "$stale_lock" ] || return 1
  stale_upload=.kosh-assimilate-$stale_transaction_id.upload
  stale_candidate=$install_dir/.kosh-assimilate-$stale_transaction_id.candidate
  [ "${1-0}" -eq 0 ] || return 0
  [ ! -L "$stale_lock/upload" ] || return 1
  [ ! -L "$stale_lock/candidate" ] || return 1
  if [ -e "$stale_lock/upload" ]; then
    recorded_upload=$(koshkit cat "$stale_lock/upload") || return 1
    [ "$recorded_upload" = "$stale_upload" ] || return 1
  fi
  if [ -e "$stale_lock/candidate" ]; then
    recorded_candidate=$(koshkit cat "$stale_lock/candidate") || return 1
    [ "$recorded_candidate" = "$stale_candidate" ] || return 1
  fi
}
recover_stale_lock() {
  stale_backup=$stale_lock/backup
  if [ ! -e "$stale_lock/committed" ] && [ -e "$stale_lock/rollback" ]; then
    remove_recorded_links "$stale_lock/links" || return 1
    if [ -e "$stale_lock/had-target" ]; then
      if [ -e "$stale_backup" ] || [ -L "$stale_backup" ]; then
        koshkit mv -f "$stale_backup" "$target" || return 1
      else
        [ -e "$target" ] || [ -L "$target" ] || return 1
      fi
    else
      koshkit rm -f "$target" || return 1
    fi
  fi
  koshkit rm -f "$stale_candidate" "$stale_upload" "$stale_backup" || return 1
  koshkit rm -f "$stale_lock/upload" "$stale_lock/candidate" \
    "$stale_lock/had-target" "$stale_lock/rollback" \
    "$stale_lock/committed" "$stale_lock/links" || return 1
  if [ "${1-0}" -eq 1 ]; then
    current_lock_record=$(koshkit cat "$lock_path") || return 1
    [ "$current_lock_record" = "$lock_record" ] || return 1
    koshkit rm -f "$lock_path" || return 1
  fi
  koshkit rm -f "$stale_lock/owner" || return 1
  koshkit rmdir "$stale_lock" || return 1
}
recover_existing_lock() {
  lock_record=$(koshkit cat "$lock_path") || return 1
  load_stale_lock_record || return 1
  recover_stale_lock 1
}
recover_orphan_locks() {
  for orphan_lock in "$install_dir"/.kosh-assimilate-*.lock; do
    [ -d "$orphan_lock" ] || continue
    [ ! -L "$orphan_lock" ] || return 1
    if [ ! -e "$orphan_lock/rollback" ] && [ ! -e "$orphan_lock/committed" ]; then
      lock_record=0:${orphan_lock##*/}
      load_stale_lock_record 1 || return 1
    else
      lock_record=$(koshkit cat "$orphan_lock/owner") || return 1
      load_stale_lock_record || return 1
    fi
    [ "$stale_lock" = "$orphan_lock" ] || return 1
    recover_stale_lock 0 || return 1
  done
}
recover_all_stale_locks() {
  if [ -e "$lock_path" ] || [ -L "$lock_path" ]; then
    recover_existing_lock || return 1
  fi
  recover_orphan_locks
}
try_install_directory() {
  directory_is_install_candidate "$1" || return 1
  lock_path=$install_dir/.kosh-assimilate.lock
  if ! recover_all_stale_locks; then
    install_rejection="the lock '$lock_path' belongs to another install or cannot be recovered"
    return 1
  fi
  directory_accepts_links || return 1
}
trap cleanup 0
trap 'exit 129' 1
trap 'exit 130' 2
trap 'exit 143' 15
install_dir=
if ! try_install_directory "${6-}"; then
  printf 'assimilate: Cannot install into %s: %s\n' "${6-}" "$install_rejection" >&2
  exit 3
fi
candidate=$install_dir/.kosh-assimilate-$2.candidate
[ ! -d "$target" ] || exit 1
recover_all_stale_locks
trap '' 1 2 15
new_lock=$install_dir/.kosh-assimilate-$2.lock
koshkit mkdir "$new_lock"
lock=$new_lock
lock_owned=1
backup=$lock/backup
printf '%s:%s\n' "$$" "${lock##*/}" > "$lock/owner"
printf '%s\n' "$upload" > "$lock/upload"
printf '%s\n' "$candidate" > "$lock/candidate"
: > "$lock/links"
if ! koshkit ln -s "$lock/owner" "$lock_path"; then
  recover_existing_lock
  koshkit ln -s "$lock/owner" "$lock_path"
fi
lock_published=1
trap 'exit 129' 1
trap 'exit 130' 2
trap 'exit 143' 15
koshkit mv "$upload" "$candidate"
if ! candidate_identity=$("$candidate" -p --mood sh -c 'printf "%s\n" "$KOSH_IDENTITY"'); then
  cleanup 1
fi
[ -n "$candidate_identity" ] && [ "$candidate_identity" = "$3" ] || cleanup 1
if [ -e "$target" ] || [ -L "$target" ]; then
  had_target=1
  : > "$lock/had-target"
fi
rollback_active=1
: > "$lock/rollback"
[ "$had_target" -eq 0 ] || koshkit mv -f "$target" "$backup"
koshkit mv -f "$candidate" "$target"
candidate=
if ! target_identity=$("$target" -p --mood sh -c 'printf "%s\n" "$KOSH_IDENTITY"'); then
  cleanup 1
fi
[ -n "$target_identity" ] && [ "$target_identity" = "$candidate_identity" ] || cleanup 1
remaining_moods=$link_moods
while [ -n "$remaining_moods" ]; do
  case $remaining_moods in
    *,*) link_name=${remaining_moods%%,*}; remaining_moods=${remaining_moods#*,} ;;
    *) link_name=$remaining_moods; remaining_moods= ;;
  esac
  [ "$link_name" = kosh ] && continue
  link_path=$install_dir/$link_name
  if [ -e "$link_path" ] || [ -L "$link_path" ]; then
    [ -L "$link_path" ] && [ "$link_path" -ef "$target" ] || cleanup 1
  else
    koshkit ln -s kosh "$link_path" || cleanup 1
    printf '%s\n' "$link_name" >> "$lock/links"
  fi
done
trap '' 1 2 15
if ! : > "$lock/committed"; then
  cleanup 1
  exit 1
fi
committed=1
rollback_active=0
koshkit rm -f "$lock/rollback"
[ "$had_target" -eq 0 ] || koshkit rm -f "$backup"
koshkit rm -f "$lock/upload" "$lock/candidate" \
  "$lock/had-target" "$lock/committed" "$lock/links"
koshkit rm -f "$lock_path"
lock_published=0
koshkit rm -f "$lock/owner"
koshkit rmdir "$lock"
lock_owned=0
lock=
koshkit rm -f "$upload"
trap - 0 1 2 15
printf '%s\n' "$target"
)SH";
inline constexpr StringView ASSIMILATE_TRANSACTION{
    ASSIMILATE_TRANSACTION_TEXT, sizeof(ASSIMILATE_TRANSACTION_TEXT) - 1};

inline constexpr char       ASSIMILATE_PREFLIGHT_TEXT[] = R"SH(set -eu
[ "$1" -eq 0 ] || set -x
expected_system=$2
expected_machine=$3
remote_system=any
remote_machine=any
if [ "$expected_system" != any ] && ! remote_system=$(uname -s 2>/dev/null); then
  printf 'assimilate-error=Cannot determine the %s operating system\n' "$assimilate_side"
  exit 125
fi
if [ "$expected_machine" != any ] && ! remote_machine=$(uname -m 2>/dev/null); then
  printf 'assimilate-error=Cannot determine the %s architecture\n' "$assimilate_side"
  exit 125
fi
case $expected_machine in
  arm64|aarch64) expected_machine=arm64 ;;
  x86_64|amd64) expected_machine=x86_64 ;;
esac
case $remote_machine in
  arm64|aarch64) remote_machine=arm64 ;;
  x86_64|amd64) remote_machine=x86_64 ;;
esac
system_matches=0
machine_matches=0
[ "$expected_system" = any ] || [ "$expected_system" = "$remote_system" ] || system_matches=1
[ "$expected_machine" = any ] || [ "$expected_machine" = "$remote_machine" ] || machine_matches=1
if [ "$system_matches" -ne 0 ] || [ "$machine_matches" -ne 0 ]; then
  printf 'assimilate-error=Cannot install a %s/%s binary on %s/%s\n' \
    "$expected_system" "$expected_machine" "$remote_system" "$remote_machine"
  exit 126
fi
requested_directory=${4-}
link_moods=${5-}
path_contains_directory() {
  wanted_directory=$1
  remaining_path=${PATH-}
  while :; do
    case $remaining_path in
      *:*) path_entry=${remaining_path%%:*}; remaining_path=${remaining_path#*:}; has_more=1 ;;
      *) path_entry=$remaining_path; has_more=0 ;;
    esac
    [ -n "$path_entry" ] || path_entry=.
    [ "$path_entry" != "$wanted_directory" ] || return 0
    [ "$has_more" -eq 1 ] || return 1
  done
}
directory_is_usable() {
  directory_is_install_candidate "$1" && directory_accepts_links
}
select_install_directory() {
  if [ -n "$requested_directory" ]; then
    directory_is_usable "$requested_directory" && return 0
    printf 'assimilate-error=Cannot install into %s: %s\n' \
      "$requested_directory" "$install_rejection"
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
  remaining_path=${PATH-}
  while :; do
    case $remaining_path in
      *:*) path_entry=${remaining_path%%:*}; remaining_path=${remaining_path#*:}; has_more=1 ;;
      *) path_entry=$remaining_path; has_more=0 ;;
    esac
    [ -n "$path_entry" ] || path_entry=.
    case $path_entry in
      /usr/local/bin|*sbin*) ;;
      "${HOME-}/.local/bin") ;;
      *) directory_is_usable "$path_entry" && return 0 ;;
    esac
    [ "$has_more" -eq 1 ] || break
  done
  printf 'assimilate-error=The %s PATH has no writable directory for kosh\n' "$assimilate_side"
  return 1
}
select_install_directory || exit 1
printf 'assimilate-install-directory=%s\n' "$install_dir"
)SH";
inline constexpr StringView ASSIMILATE_PREFLIGHT{
    ASSIMILATE_PREFLIGHT_TEXT, sizeof(ASSIMILATE_PREFLIGHT_TEXT) - 1};

constexpr StringView ASSIMILATE_TRANSACTION_LOCK_SCRIPT{
    "koshkit flock --transaction-held-lock \"$@\""};

constexpr StringView FLOCK_KEEPER_SCRIPT{"koshkit flock \"$@\""};

constexpr StringView GOODCORE_DEBUG_SCRIPT_PRELUDE{
    "#!/bin/sh\nroot=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd -P) || "
    "exit 1\n"};

constexpr StringView GOODCORE_GDB_SCRIPT_HEAD{
    "exec gdb -ex \"set sysroot $root/root\" -ex \"set solib-search-path "
    "$root/root\" \"$root/root/\""};

constexpr StringView GOODCORE_GDB_SCRIPT_TAIL{" \"$root/dump/core\" \"$@\"\n"};

constexpr StringView GOODCORE_LLDB_SCRIPT_HEAD{
    "exec lldb -O \"platform select host --sysroot '$root/root'\" --core "
    "\"$root/dump/core\" \"$root/root/\""};

constexpr StringView GOODCORE_LLDB_SCRIPT_TAIL{" \"$@\"\n"};

} /* namespace koshka */
