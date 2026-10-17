/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the sleep utility. It parses and sums suffixed
 * durations, handles an unbounded duration, waits through the platform clock,
 * and reports interruption.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL("duration ...", "Pause for the sum of the given durations.");

REGISTER_KOSHKIT_UTIL_FLAGS(Sleep);

namespace koshka::koshkit {

fn
Sleep::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  f64  total_seconds        = 0.0;
  bool should_sleep_forever = false;

  for (usize operand_index = 0; operand_index < operands.count();
       operand_index++)
  {
    let const &operand       = operands[operand_index];
    let const  seconds_value = parse_koshkit_duration_seconds(
        operand.view(), operand_locations[operand_index],
        cxt.scratch_allocator());

    if (__builtin_isinf(seconds_value)) {
      should_sleep_forever = true;
      continue;
    }

    total_seconds += seconds_value;
    if (__builtin_isinf(total_seconds)) should_sleep_forever = true;
  }

  constexpr f64 STEP_SECONDS = 60.0 * 60.0 * 24.0;

  f64 remaining_seconds =
      should_sleep_forever ? __builtin_inf() : total_seconds;

  /* the wait runs in steps of at most one day, so sleep_for_seconds() gets a
     value that fits time_t and the DWORD milliseconds of Sleep(). Sleep() on
     Windows does not return on an interrupt. */
  while (remaining_seconds > 0.0 && !os::INTERRUPT_REQUESTED) {
    let const step_seconds =
        remaining_seconds < STEP_SECONDS ? remaining_seconds : STEP_SECONDS;

    remaining_seconds -= step_seconds;
    os::sleep_for_seconds(step_seconds);
  }

  return os::INTERRUPT_REQUESTED ? 130 : 0;
}

} /* namespace koshka::koshkit */
