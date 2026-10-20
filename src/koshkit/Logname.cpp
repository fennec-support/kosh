/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the logname utility. It queries the platform
 * login-session identity and reports a failure when no login name is available.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL("", "The logname utility can write the login name.");

REGISTER_KOSHKIT_UTIL_FLAGS(Logname);

namespace koshka::koshkit {

fn
Logname::execute(const ExecContext               &ec,
                 EvalContext                     &cxt,
                 const ArrayList<String>         &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "Unexpected operand '" + operands[0] + "'");

    return 2;
  }

  let const name = os::get_login_user();

  if (!name.has_value()) {
    report_soft_koshkit_util_error(ec, cxt, args[0].view(),
                                   "Login name is unavailable");

    return 1;
  }

  /* at this point: name holds the login name, and operands is empty. */
  ec.print_to_stdout(name->view());
  ec.print_to_stdout("\n");

  return 0;
}

} /* namespace koshka::koshkit */
