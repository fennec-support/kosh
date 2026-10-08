/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements shared user, group, and recursive ownership helpers.
 * Keeping traversal and identifier resolution here prevents chown, chgrp,
 * and id from growing subtly different ownership behavior.
 */

#include "UtilsOwnership.hpp"

#include "Eval.hpp"
#include "Koshkit.hpp"
#include "Platform.hpp"

namespace koshka::utils {

namespace {

struct ownership_directory_identity
{
  u64 device_id;
  u64 file_id;
};

static fn parse_numeric_id(StringView text) wontthrow -> Maybe<u32>
{
  if (text.is_empty()) return None;
  u64 value = 0;
  for (usize position = 0; position < text.length; position++) {
    let const byte = text[position];
    if (byte < '0' || byte > '9') return None;
    value = value * 10 + static_cast<u64>(byte - '0');
    if (value > UINT32_MAX) return None;
  }
  return static_cast<u32>(value);
}

static fn change_path_ownership_recursive(
    const ExecContext &ec, EvalContext &cxt, StringView utility_name,
    const Path &path, i64 owner_id, i64 group_id,
    ArrayList<ownership_directory_identity> &active_directories,
    const os::file_status *known_path_status = nullptr,
    const os::file_status *known_followed_status = nullptr,
    bool was_followed_status_queried = false,
    ownership_traversal_mode traversal_mode =
        ownership_traversal_mode::Recursive,
    ownership_symlink_mode symlink_mode = ownership_symlink_mode::Follow,
    ownership_symlink_mode nested_symlink_mode = ownership_symlink_mode::Follow,
    Maybe<u64> root_device_id = {}) throws -> bool
{
  let const is_recursive =
      traversal_mode != ownership_traversal_mode::SinglePath;
  let const should_follow_symlink =
      symlink_mode == ownership_symlink_mode::Follow;
  let const should_follow_nested_symlinks =
      nested_symlink_mode == ownership_symlink_mode::Follow;
  os::file_status path_status{};
  if (known_path_status != nullptr) {
    path_status = *known_path_status;
  } else if (!os::stat_path(path.view(), path_status)) {
    koshkit::report_soft_koshkit_error(
        ec, cxt,
        utility_name + ": cannot access '" + path.text() +
            "': " + os::last_system_error_message());
    return false;
  }

  let const is_symlink = os::file_type_letter(path_status.mode) == 'l';
  let const follow_mode = !is_symlink || should_follow_symlink
                              ? os::symlink_follow_mode::Follow
                              : os::symlink_follow_mode::NoFollow;
  if (!os::set_file_owner(path.view(), owner_id, group_id, follow_mode)) {
    koshkit::report_soft_koshkit_error(
        ec, cxt,
        utility_name + ": cannot change ownership of '" + path.text() +
            "': " + os::last_system_error_message());
    return false;
  }

  if (!is_recursive) return true;
  if (is_symlink && !should_follow_symlink) return true;

  os::file_status followed_status{};
  if (is_symlink) {
    if (was_followed_status_queried) {
      if (known_followed_status == nullptr) return true;
      followed_status = *known_followed_status;
    } else if (!os::stat_path_following(path.view(), followed_status)) {
      return true;
    }
  } else {
    followed_status = path_status;
  }
  if (os::file_type_letter(followed_status.mode) != 'd') return true;

  if (traversal_mode == ownership_traversal_mode::RecursiveOneFileSystem) {
    if (!root_device_id.has_value())
      root_device_id = followed_status.device_id;
    else if (followed_status.device_id != *root_device_id)
      return true;
  }

  if (followed_status.has_file_identity) {
    for (let const &identity : active_directories) {
      if (identity.device_id == followed_status.device_id &&
          identity.file_id == followed_status.file_id)
      {
        koshkit::report_soft_koshkit_error(
            ec, cxt,
            utility_name + ": recursive directory loop at '" + path.text() +
                "'");
        return false;
      }
    }

    active_directories.push(
        {followed_status.device_id, followed_status.file_id});
  }
  defer
  {
    if (followed_status.has_file_identity) active_directories.pop_back();
  };

  let const directory_scratch = cxt.expansion_store().scratch_arena().mark();
  defer { cxt.expansion_store().scratch_arena().release(directory_scratch); };
  let children =
      os::list_directory_status(path.view(), cxt.scratch_allocator());
  if (!children.has_value()) {
    koshkit::report_soft_koshkit_error(
        ec, cxt,
        utility_name + ": cannot read directory '" + path.text() +
            "': " + os::last_system_error_message());
    return false;
  }

  let child_paths = ArrayList<Path>{cxt.scratch_allocator()};
  let followed_child_positions = ArrayList<usize>{cxt.scratch_allocator()};
  let followed_child_statuses =
      ArrayList<os::file_status>{cxt.scratch_allocator()};
  let followed_batch = os::Batch{cxt.scratch_allocator()};
  child_paths.reserve(children->count());
  if (should_follow_nested_symlinks) {
    followed_child_positions.reserve(children->count());
    followed_child_statuses.reserve(children->count());
    followed_batch.reserve(children->count());
  }

  for (usize child_position = 0; child_position < children->count();
       child_position++)
  {
    let const &child_entry = (*children)[child_position];
    let child = Path{path.view(), cxt.scratch_allocator()};
    child.append(child_entry.child.name.view());
    child_paths.push(steal(child));

    let const is_child_symlink =
        child_entry.child.kind == Path::entry_kind::Symlink ||
        (child_entry.has_status &&
         os::file_type_letter(child_entry.status.mode) == 'l');
    if (!should_follow_nested_symlinks || !is_child_symlink) continue;

    followed_child_positions.push(child_position);
    followed_child_statuses.push({});
    followed_batch.add(os::batch_operation::stat(
        child_paths.back(), followed_child_statuses.back()));
  }

  let followed_results = ArrayList<os::batch_result>{cxt.scratch_allocator()};
  if (followed_batch.count() != 0) followed_batch.execute(followed_results);

  bool did_succeed = true;
  usize followed_position = 0;
  for (usize child_position = 0; child_position < children->count();
       child_position++)
  {
    if (os::INTERRUPT_REQUESTED) return did_succeed;
    let const &child_entry = (*children)[child_position];
    let const child_status =
        child_entry.has_status ? &child_entry.status : nullptr;
    let known_child_followed_status =
        static_cast<const os::file_status *>(nullptr);
    let was_child_followed_status_queried = false;
    if (followed_position < followed_child_positions.count() &&
        followed_child_positions[followed_position] == child_position)
    {
      was_child_followed_status_queried = true;
      if (followed_results[followed_position].error_number == 0)
        known_child_followed_status =
            &followed_child_statuses[followed_position];
      followed_position++;
    }

    if (!change_path_ownership_recursive(
            ec, cxt, utility_name, child_paths[child_position], owner_id,
            group_id, active_directories, child_status,
            known_child_followed_status, was_child_followed_status_queried,
            traversal_mode,
            should_follow_nested_symlinks ? ownership_symlink_mode::Follow
                                          : ownership_symlink_mode::NoFollow,
            should_follow_nested_symlinks ? ownership_symlink_mode::Follow
                                          : ownership_symlink_mode::NoFollow,
            root_device_id))
      did_succeed = false;
  }

  return did_succeed;
}

}

fn resolve_user_id(StringView text) throws -> Maybe<u32>
{
  if (let const numeric = parse_numeric_id(text); numeric.has_value())
    return numeric;
  return os::username_to_uid(text);
}

fn resolve_group_id(StringView text) throws -> Maybe<u32>
{
  if (let const numeric = parse_numeric_id(text); numeric.has_value())
    return numeric;
  return os::groupname_to_gid(text);
}

fn change_operands_ownership(const ExecContext &ec, EvalContext &cxt,
                             StringView utility_name,
                             const ArrayList<String> &operands,
                             const ownership_request &request) throws -> i32
{
  let const traversal_mode =
      !request.is_recursive ? ownership_traversal_mode::SinglePath
      : request.is_one_file_system
          ? ownership_traversal_mode::RecursiveOneFileSystem
          : ownership_traversal_mode::Recursive;
  let traversal_position = request.command_line_follow_position;
  if (request.follow_position > traversal_position)
    traversal_position = request.follow_position;
  if (request.physical_position > traversal_position)
    traversal_position = request.physical_position;

  let const should_follow_nested =
      request.follow_position == traversal_position && traversal_position != 0;
  let const should_follow_command_line =
      should_follow_nested ||
      (request.command_line_follow_position == traversal_position &&
       traversal_position != 0);
  let const should_follow_argument =
      !request.should_change_symlink &&
      (!request.is_recursive || should_follow_command_line);
  i32 status = 0;

  for (usize index = 1; index < operands.count(); index++) {
    if (os::INTERRUPT_REQUESTED) return 130;
    let active_directories =
        ArrayList<ownership_directory_identity>{heap_allocator()};
    if (!change_path_ownership_recursive(
            ec, cxt, utility_name,
            Path{operands[index].view(), cxt.scratch_allocator()},
            request.owner_id, request.group_id, active_directories, nullptr,
            nullptr, false, traversal_mode,
            should_follow_argument ? ownership_symlink_mode::Follow
                                   : ownership_symlink_mode::NoFollow,
            should_follow_nested ? ownership_symlink_mode::Follow
                                 : ownership_symlink_mode::NoFollow))
      status = 1;
    if (os::INTERRUPT_REQUESTED) return 130;
  }

  return status;
}

}
