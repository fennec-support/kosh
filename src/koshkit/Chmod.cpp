/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the chmod utility. It applies octal or symbolic modes to
 * files and recursively traverses directories when requested.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("[-R] [--one-file-system] mode file ...",
                  "The chmod utility can change file permission modes.");

FLAG(CHMOD_RECURSIVE,
     Bool,
     'R',
     "recursive",
     "Change directories and their contents recursively.");
FLAG(CHMOD_ONE_FILE_SYSTEM,
     Bool,
     '\0',
     "one-file-system",
     "Skip directories on file systems other than each operand's file system.");

REGISTER_KOSHKIT_UTIL_FLAGS(Chmod);

namespace koshka::koshkit {

struct chmod_state
{
  const ExecContext &ec;
  EvalContext       &cxt;
  StringView         expression;
  u32                creation_mask;
};

static fn
change_mode(void *context, const os::tree_walk_entry &entry) throws
    -> bool
{
  let const &state = *static_cast<chmod_state *>(context);

  if (entry.is_other_file_system) return true;

  os::file_status status = *entry.status;

  /* A symlink operand is changed through its target. A nested symlink is
     skipped. */
  if (os::file_type_letter(status.mode) == 'l') {
    if (!entry.is_operand) return true;

    if (!os::stat_path_following(entry.path, status)) {
      report_soft_koshkit_util_error(state.ec, state.cxt, "chmod",
                                     "Cannot access '" + entry.path + "': " +
                                         os::last_system_error_message());

      return false;
    }
  }

  let const parsed =
      utils::parse_file_mode(state.expression, status.mode, state.creation_mask,
                             os::file_type_letter(status.mode) == 'd'
                                 ? utils::file_kind_mode::Directory
                                 : utils::file_kind_mode::Regular);

  /* at this point: the mode expression was accepted by Chmod::execute() for
     a regular file, and parsing does not fail based on the file kind, so the
     parse cannot fail here. */
  ASSERT(parsed.has_value());

  if (os::set_tree_entry_mode(entry, *parsed)) return true;

  report_soft_koshkit_util_error(state.ec, state.cxt, "chmod",
                                 "Cannot change mode of '" + entry.path +
                                     "': " + os::last_system_error_message());

  return false;
}

static fn
report_walk_failure(void *context, StringView message) throws -> void
{
  let const &state = *static_cast<chmod_state *>(context);

  report_soft_koshkit_util_error(state.ec, state.cxt, "chmod", message);
}

fn
Chmod::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  let const expression = operands[0].view();

  if (!utils::parse_file_mode(expression, 0, 0, utils::file_kind_mode::Regular)
           .has_value())
  {
    KOSHKIT_REPORT_ERROR_AT(
        operand_locations[0], "Invalid mode '" + operands[0] + "'",
        "Use an octal mode up to 7777 or symbolic clauses such as u+x,g-w");

    return 1;
  }

  /* at this point: expression parses as a mode for a regular file, and
     operands holds the mode followed by at least one file. */
  i32 status = 0;
  let state  = chmod_state{ec, cxt, expression, os::get_file_creation_mask()};
  let const hooks =
      os::tree_walk_hooks{&state, change_mode, report_walk_failure};
  let const options = os::tree_walk_options{
      FLAG_CHMOD_RECURSIVE.is_enabled(),
      FLAG_CHMOD_ONE_FILE_SYSTEM.is_enabled(), true, false, true};

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

} /* namespace koshka::koshkit */
