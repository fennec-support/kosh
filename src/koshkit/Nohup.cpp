/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the nohup utility. It redirects terminal streams when
 * required, ignores hangup delivery, resolves the command, and returns the
 * command status.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../platform/Platform.hpp"

KOSHKIT_UTIL_DECL("utility [argument ...]",
                  "Run a command that ignores terminal hangups.");

REGISTER_KOSHKIT_UTIL_FLAGS(Nohup);

namespace koshka::koshkit {

fn
Nohup::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations,
                                 {.should_stop_at_first_operand = true});

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  let const home_value = cxt.get_variable_value("HOME");
  let const home = home_value.has_value() ? home_value->view() : StringView{};

  cxt.prepare_child_environment();

  let const result = os::run_nohup(
      operands, {ec.in_fd.value_or(KOSH_STDIN), ec.out_fd.value_or(KOSH_STDOUT),
                 ec.err_fd.value_or(KOSH_STDERR), home});

  if (!result.has_value()) {
    let const exec_errno = errno;

    KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[0], "run", operands[0]);

    return exec_errno == ENOENT ? 127 : 126;
  }

  return *result;
}

} /* namespace koshka::koshkit */
