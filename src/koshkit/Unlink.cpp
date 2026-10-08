/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the unlink utility. It validates one operand, rejects
 * real directories, and removes the named file or symbolic link without
 * traversal.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL("file", "The unlink utility removes the single named file.");

REGISTER_KOSHKIT_UTIL_FLAGS(Unlink);

namespace koshka::koshkit {

cold fn Unlink::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() > 1) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                            "extra operand '" + operands[1] + "'");
    return 1;
  }

  /* unlink(2) removes the link itself, so a symlink to a directory passes and
     only a real directory is refused. */
  let const &target = operands[0];
  let const target_path = Path{target.view(), cxt.scratch_allocator()};
  if (target_path.is_directory() && !target_path.is_symbolic_link()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0], "cannot unlink '" + target +
                                                      "': it is a directory");
    return 1;
  }

  if (!remove_path(target.view(), cxt.scratch_allocator(),
                   removal_mode::SinglePath))
  {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "cannot unlink '" + target +
                                "': " + os::last_system_error_message());
    return 1;
  }
  return 0;
}

} /* namespace koshka::koshkit */
