/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the realpath utility. It canonicalizes each operand
 * through the platform filesystem interface and reports unresolved paths
 * independently.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Path.hpp"

KOSHKIT_UTIL_DECL(
    "path ...",
    "The realpath utility prints the absolute, normalized form of each path.");

REGISTER_KOSHKIT_UTIL_FLAGS(Realpath);

namespace koshka::koshkit {

cold fn Realpath::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const allocator = cxt.scratch_allocator();
  let output = String{allocator};
  i32 status = 0;
  for (usize operand_index = 0; operand_index < operands.count();
       operand_index++)
  {
    let const &operand = operands[operand_index];
    let const resolved = os::canonical_path(Path{operand.view(), allocator});
    if (!resolved) {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[operand_index],
                              "'" + operand +
                                  "': " + os::last_system_error_message());
      status = 1;
      continue;
    }

    output += resolved->text().view();
    output += '\n';
  }
  ec.print_to_stdout(output);
  return status;
}

}
