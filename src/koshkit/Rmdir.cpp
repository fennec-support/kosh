/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the rmdir utility. It removes empty operand directories
 * and can continue upward through empty parent directories.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL("[-p] directory ...",
                  "The rmdir utility removes each empty directory.");

FLAG(RMDIR_PARENTS, Bool, 'p', "", "Remove empty parent directories.");

REGISTER_KOSHKIT_UTIL_FLAGS(Rmdir);

namespace koshka::koshkit {

fn Rmdir::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const allocator = cxt.scratch_allocator();
  i32 status = 0;
  for (usize operand_index = 0; operand_index < operands.count();
       operand_index++)
  {
    let const &operand = operands[operand_index];
    if (!os::remove_directory(operand.view())) {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[operand_index],
                              "failed to remove '" + operand +
                                  "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    if (FLAG_RMDIR_PARENTS.is_enabled()) {
      let current = Path{operand.view(), allocator};
      loop
      {
        let parent = current.parent();
        if (parent.is_empty() || parent == current) break;
        if (!os::remove_directory(parent.view())) {
          KOSHKIT_REPORT_ERROR_AT(operand_locations[operand_index],
                                  "failed to remove '" + parent.text() +
                                      "': " + os::last_system_error_message());
          status = 1;
          break;
        }
        current = steal(parent);
      }
    }
  }
  return status;
}

}
