/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements shared user, group, and recursive ownership helpers.
 * Keeping traversal and identifier resolution here prevents chown, chgrp,
 * and id from growing subtly different ownership behavior.
 */

#include "UtilsOwnership.hpp"

#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/ArrayList.hpp"
#include "../base/String.hpp"
#include "../platform/Platform.hpp"

namespace koshka::utils {

namespace {

struct ownership_state
{
  const ExecContext           &ec;
  EvalContext                 &cxt;
  i64                          owner_id;
  i64                          group_id;
  const os::tree_walk_options &options;
};

static fn
parse_numeric_id(StringView text) wontthrow -> Maybe<u32>
{
  if (text.is_empty()) return None;

  u64 value = 0;

  for (usize position = 0; position < text.length; position++) {
    let const byte = text[position];

    if (byte < '0' || byte > '9') {
      return None;
    }

    value = value * 10 + static_cast<u64>(byte - '0');
    if (value >= UINT32_MAX) return None;
  }

  /* at this point: text is non-empty, holds only decimal digits, and value
     fits in u32. */
  return static_cast<u32>(value);
}

static fn
change_entry_ownership(void                      *context,
                       const os::tree_walk_entry &entry) throws -> bool
{
  let const &state = *static_cast<ownership_state *>(context);

  /* a symlink is followed only when the mode asks for it. A nested entry
     keeps a swapped-in symlink from redirecting the change outside the
     tree. */
  let const follow_mode =
      (entry.is_operand ? state.options.should_follow_operands
                        : state.options.should_follow_nested)
          ? os::symlink_follow_mode::Follow
          : os::symlink_follow_mode::NoFollow;

  if (os::set_tree_entry_owner(entry, state.owner_id, state.group_id,
                               follow_mode))
  {
    return true;
  }

  koshkit::report_soft_koshkit_error(
      state.ec, state.cxt,
      "cannot change ownership of '" + entry.path +
          "': " + os::last_system_error_message());

  return false;
}

static fn
report_ownership_walk_failure(void *context, StringView message) throws
    -> void
{
  let const &state = *static_cast<ownership_state *>(context);

  koshkit::report_soft_koshkit_error(state.ec, state.cxt, message);
}

} /* namespace */

fn
resolve_user_id(StringView text) throws -> Maybe<u32>
{
  if (let const numeric = parse_numeric_id(text); numeric.has_value())
    return numeric;

  return os::username_to_uid(text);
}

fn
resolve_group_id(StringView text) throws -> Maybe<u32>
{
  if (let const numeric = parse_numeric_id(text); numeric.has_value())
    return numeric;

  return os::groupname_to_gid(text);
}

namespace {

struct id_name_entry
{
  id_name_entry(u32 id, String name) : id(id), name(steal(name)) {}

  u32    id;
  String name;
};

struct id_name_cache
{
  static constexpr usize ENTRY_LIMIT    = 64;
  static constexpr u64   LIFETIME_NANOS = 1'000'000'000;

  id_name_cache() : users(heap_allocator()), groups(heap_allocator()) {}

  ArrayList<id_name_entry> users;
  ArrayList<id_name_entry> groups;
  u64                      filled_nanos{0};
};

} /* namespace */

static fn
cached_name_or_id(u32 id, bool is_group, Allocator allocator) throws
    -> String
{
  static thread_local id_name_cache cache;

  let const now = os::monotonic_nanos();

  if (cache.filled_nanos == 0 ||
      now - cache.filled_nanos > id_name_cache::LIFETIME_NANOS)
  {
    cache.users.clear();
    cache.groups.clear();
    cache.filled_nanos = now;
  }

  let &entries = is_group ? cache.groups : cache.users;

  for (let const &entry : entries) {
    if (entry.id == id) return String{allocator, entry.name.view()};
  }

  let const looked_up =
      is_group ? os::gid_to_groupname(id) : os::uid_to_username(id);

  if (entries.count() >= id_name_cache::ENTRY_LIMIT) entries.clear();

  entries.push(id_name_entry{
      id, looked_up.has_value() ? String{heap_allocator(), looked_up->view()}
                                : String::from(id, heap_allocator())
  });

  return String{allocator, entries.back().name.view()};
}

fn
user_name_or_id(u32 id, Allocator allocator) throws -> String
{
  return cached_name_or_id(id, false, allocator);
}

fn
group_name_or_id(u32 id, Allocator allocator) throws -> String
{
  return cached_name_or_id(id, true, allocator);
}

fn
change_operands_ownership(const ExecContext       &ec,
                          EvalContext             &cxt,
                          const ArrayList<String> &operands,
                          const ownership_request &request) throws
    -> i32
{
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
  let const options = os::tree_walk_options{
      request.is_recursive, request.is_one_file_system, should_follow_argument,
      should_follow_nested, false};
  let state =
      ownership_state{ec, cxt, request.owner_id, request.group_id, options};
  let const hooks  = os::tree_walk_hooks{&state, change_entry_ownership,
                                        report_ownership_walk_failure};
  i32       status = 0;

  for (usize index = 1; index < operands.count(); index++) {
    if (os::INTERRUPT_REQUESTED) return 130;

    let const operand_scratch = cxt.expansion_store().scratch_arena().mark();
    defer {
      cxt.expansion_store().scratch_arena().release(operand_scratch);
    };

    if (!os::walk_tree(operands[index].view(), options, hooks)) status = 1;
    if (os::INTERRUPT_REQUESTED) return 130;
  }

  return status;
}

} /* namespace koshka::utils */
