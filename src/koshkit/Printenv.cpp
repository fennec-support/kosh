/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the printenv utility in koshkit. It writes the whole
 * process environment or the values of named variables in operand order.
 */

#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL("[name ...]",
                  "The printenv utility writes environment variables.");

REGISTER_KOSHKIT_UTIL_FLAGS(Printenv);

namespace koshka::koshkit {

fn Printenv::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) {
    print_environment(ec, cxt);
    return 0;
  }

  cxt.prepare_child_environment();
  let output = String{cxt.scratch_allocator()};
  i32 status = 0;
  for (let const &name : operands) {
    let const value = os::get_environment_variable(name.view());
    if (!value.has_value()) {
      status = 1;
      continue;
    }

    output += value->view();
    output += '\n';
  }

  ec.print_to_stdout(output);
  return status;
}

}
