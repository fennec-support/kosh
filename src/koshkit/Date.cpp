/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the date utility. It queries the platform clock and
 * formats local or Coordinated Universal Time with strftime directives.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../util/Utils.hpp"

KOSHKIT_UTIL_DECL("[-u] [+format]",
                  "The date utility can write the date and time.");

FLAG(DATE_UTC, Bool, 'u', "utc", "Use Coordinated Universal Time.");

REGISTER_KOSHKIT_UTIL_FLAGS(Date);

namespace koshka::koshkit {

fn
Date::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.count() > 1) return report_usage_error(ec, cxt, args[0].view());
  if (!operands.is_empty() && (operands[0].is_empty() || operands[0][0] != '+'))
  {
    report_soft_koshkit_error(ec, cxt, operand_locations[0],
                              "Setting the system clock is unsupported");

    return 1;
  }

  /* at this point: operands holds no operand or one operand that starts with
     '+'. */
  let const format = operands.is_empty() ? StringView{"%a %b %e %H:%M:%S %Z %Y"}
                                         : operands[0].view().substring(1);
  let const formatted = os::format_time(
      format, os::current_unix_seconds(),
      FLAG_DATE_UTC.is_enabled() ? os::time_zone::Utc : os::time_zone::Local);

  if (!formatted.has_value()) {
    let const location =
        operands.is_empty() ? ec.source_location() : operand_locations[0];

    report_soft_koshkit_error(ec, cxt, location,
                              "Cannot format the current time, or the "
                              "result exceeds 8192 bytes");

    return 1;
  }

  ec.print_to_stdout(*formatted);
  ec.print_to_stdout("\n");

  return 0;
}

} /* namespace koshka::koshkit */
