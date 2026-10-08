/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the link utility. It validates two operands and creates
 * one hard link through the platform filesystem interface.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL("file1 file2",
                  "The link utility creates a hard link to a file.");

REGISTER_KOSHKIT_UTIL_FLAGS(Link);

namespace koshka::koshkit {

fn Link::execute(const ExecContext &ec, EvalContext &cxt,
                 const ArrayList<String> &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() > 2) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[2],
                            "extra operand '" + operands[2] + "'");
    return 1;
  }

  if (os::create_hard_link(operands[0].view(), operands[1].view())) return 0;
  KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[1], "create", operands[1]);
  return 1;
}

} /* namespace koshka::koshkit */
