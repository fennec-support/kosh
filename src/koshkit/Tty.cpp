/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the tty utility. It queries the terminal attached to
 * standard input and supports silent status-only operation.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL("[-s]", "The tty utility writes the terminal name.");

FLAG(TTY_SILENT, Bool, 's', "silent", "Write no output.");

REGISTER_KOSHKIT_UTIL_FLAGS(Tty);

namespace koshka::koshkit {

fn Tty::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args,
                const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "unexpected operand '" + operands[0] + "'");
    return 2;
  }

  let const name = os::terminal_name(ec.in_fd.value_or(KOSH_STDIN));
  if (!name.has_value()) {
    if (!FLAG_TTY_SILENT.is_enabled()) ec.print_to_stdout("not a tty\n");
    return 1;
  }
  if (!FLAG_TTY_SILENT.is_enabled()) {
    ec.print_to_stdout(name->view());
    ec.print_to_stdout("\n");
  }
  return 0;
}

} /* namespace koshka::koshkit */
