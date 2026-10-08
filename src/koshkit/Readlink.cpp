/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the readlink utility. It reads each symbolic-link target
 * through the platform interface and controls the trailing newline.
 */

#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL("[-n] file ...",
                  "The readlink utility prints the target of a symbolic link.");

FLAG(READLINK_NO_NEWLINE, Bool, 'n', "",
     "Do not print a trailing newline after a single file.");

REGISTER_KOSHKIT_UTIL_FLAGS(Readlink);

namespace koshka::koshkit {

fn Readlink::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const should_end_lines =
      operands.count() > 1 || !FLAG_READLINK_NO_NEWLINE.is_enabled();
  i32 status = 0;
  for (usize i = 0; i < operands.count(); i++) {
    let target = os::read_symlink(operands[i].view(), cxt.scratch_allocator());
    if (!target.has_value()) {
      report_soft_koshkit_util_error(
          ec, cxt, operand_locations[i], args[0].view(),
          "'" + operands[i] + "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    if (should_end_lines) target->push('\n');
    ec.print_to_stdout(target->view());
  }

  return status;
}

}
