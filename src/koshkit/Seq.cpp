/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the seq utility. It parses signed integer bounds and
 * increments, detects unreachable or overflowing ranges, and writes each
 * generated value.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL(
    "[first [increment]] last",
    "The seq utility prints a sequence of integers from first to last.");

REGISTER_KOSHKIT_UTIL_FLAGS(Seq);

namespace koshka::koshkit {

fn Seq::execute(const ExecContext &ec, EvalContext &cxt,
                const ArrayList<String> &args,
                const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(
      args, arg_locations, {.should_accept_negative_number_operand = true});

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  i64 first = 1;
  i64 increment = 1;
  i64 last = 0;
  let const do_parse_integer = [&](usize operand_position, i64 &value)
                                   throws -> bool {
    let const parsed = operands[operand_position].view().to<i64>();
    if (parsed.is_error()) {
      KOSHKIT_REPORT_ERROR_AT(
          operand_locations[operand_position],
          "invalid integer argument '" + operands[operand_position] + "'",
          "use a decimal integer from -9223372036854775808 through "
          "9223372036854775807");
      return false;
    }

    value = parsed.value();
    return true;
  };

  if (operands.count() == 1) {
    if (!do_parse_integer(0, last)) return 1;
  } else if (operands.count() == 2) {
    if (!do_parse_integer(0, first) || !do_parse_integer(1, last)) return 1;
  } else if (operands.count() == 3) {
    if (!do_parse_integer(0, first) || !do_parse_integer(1, increment) ||
        !do_parse_integer(2, last))
    {
      return 1;
    }
  } else {
    KOSHKIT_REPORT_ERROR_AT(
        operand_locations[3], "extra operand '" + operands[3] + "'",
        "use `seq LAST`, `seq FIRST LAST`, or `seq FIRST STEP LAST`");
    return 1;
  }

  if (increment == 0) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                            "the increment must not be zero",
                            "use a nonzero step, such as `seq 1 2 10`");
    return 1;
  }

  let output = String{cxt.scratch_allocator()};
  static constexpr usize OUTPUT_BUFFER_LENGTH = 64 * 1024;
  output.reserve(OUTPUT_BUFFER_LENGTH);
  char value_text[21];
  if (increment > 0)
    for (i64 value = first; value <= last; value += increment) {
      output += utils::int_to_text_into(value, value_text, sizeof(value_text));
      output += '\n';
      if (output.count() >= OUTPUT_BUFFER_LENGTH) {
        ec.print_to_stdout(output);
        output.clear();
      }
      let const would_step_overflow = value > INT64_MAX - increment;
      if (would_step_overflow) break;
    }
  else
    for (i64 value = first; value >= last; value += increment) {
      output += utils::int_to_text_into(value, value_text, sizeof(value_text));
      output += '\n';
      if (output.count() >= OUTPUT_BUFFER_LENGTH) {
        ec.print_to_stdout(output);
        output.clear();
      }
      let const would_step_underflow = value < INT64_MIN - increment;
      if (would_step_underflow) break;
    }

  ec.print_to_stdout(output);
  return 0;
}

}
