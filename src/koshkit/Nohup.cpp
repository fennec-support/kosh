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
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL(
    "utility [argument ...]",
    "The nohup utility invokes a command that ignores terminal hangups.");

REGISTER_KOSHKIT_UTIL_FLAGS(Nohup);

namespace koshka::koshkit {

fn Nohup::execute(const ExecContext &ec, EvalContext &cxt,
                  const ArrayList<String> &args,
                  const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  let command = ArrayList<String>{cxt.scratch_allocator()};
  for (let const &operand : operands)
    command.push(operand.clone());
  let const home_value = cxt.get_variable_value("HOME");
  let const home = home_value.has_value() ? home_value->view() : StringView{};
  cxt.prepare_child_environment();
  let const result = os::run_nohup(
      command, {ec.in_fd.value_or(KOSH_STDIN), ec.out_fd.value_or(KOSH_STDOUT),
                ec.err_fd.value_or(KOSH_STDERR), home});
  if (!result.has_value()) {
    report_soft_koshkit_util_error(
        ec, cxt, operand_locations[0], args[0].view(),
        "cannot run '" + operands[0] + "': " + os::last_system_error_message());
    return 126;
  }

  return *result;
}

} /* namespace koshka::koshkit */
