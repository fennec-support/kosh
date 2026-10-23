/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the nice utility. It parses a priority adjustment,
 * resolves the requested program, starts it with the adjusted scheduling
 * priority, and returns its status.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL("[-n increment] utility [argument ...]",
                  "Run a command with an adjusted scheduling priority.");

FLAG(NICE_INCREMENT,
     String,
     'n',
     "increment",
     "Add this value to the inherited priority.");

REGISTER_KOSHKIT_UTIL_FLAGS(Nice);

namespace koshka::koshkit {

fn
Nice::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations,
                                 {.should_stop_at_first_operand = true});

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  i64 increment = 10;

  if (FLAG_NICE_INCREMENT.is_set()) {
    let const parsed = utils::parse_decimal_i64(FLAG_NICE_INCREMENT.value());

    if (parsed.is_error() || parsed.value() < INT32_MIN ||
        parsed.value() > INT32_MAX)
    {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_NICE_INCREMENT.value_location(),
          "Invalid increment '" + FLAG_NICE_INCREMENT.value() + "'",
          "Use a decimal integer from -2147483648 through 2147483647");

      return 1;
    }

    increment = parsed.value();
  }

  /* at this point: operands is not empty, and increment fits in i32. */

  cxt.prepare_child_environment();

  let const result = os::run_nice(operands, static_cast<i32>(increment));

  if (!result.has_value()) {
    let const exec_errno = errno;

    KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[0], "run", operands[0]);

    return exec_errno == ENOENT ? 127 : 126;
  }

  return *result;
}

} /* namespace koshka::koshkit */
