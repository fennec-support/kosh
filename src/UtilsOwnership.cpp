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

struct ownership_state
{
  const ExecContext           &ec;
  EvalContext                 &cxt;
  StringView                   utility_name;
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
    if (value > UINT32_MAX) return None;
  }

  return static_cast<u32>(value);
}

static fn
change_entry_ownership(void                      *context,
                       const os::tree_walk_entry &entry) throws -> bool
{
  let const &state = *static_cast<ownership_state *>(context);

  /* A symlink is followed only when the mode asks for it. A nested entry
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

fn
change_operands_ownership(const ExecContext       &ec,
                          EvalContext             &cxt,
                          StringView               utility_name,
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
  let state = ownership_state{
      ec, cxt, utility_name, request.owner_id, request.group_id, options};
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
