/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the nproc utility. It selects the available or
 * configured logical processor count and subtracts a bounded ignored count.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL(
    "[--all] [--ignore=count]",
    "The nproc utility prints the number of available logical processors.");

FLAG(NPROC_ALL, Bool, '\0', "all", "Print the configured processor count.");
FLAG(NPROC_IGNORE, String, '\0', "ignore",
     "Exclude up to this many processors from the result.");

REGISTER_KOSHKIT_UTIL_FLAGS(Nproc);

namespace koshka::koshkit {

cold fn Nproc::execute(
    const ExecContext &ec, EvalContext &cxt, const ArrayList<String> &args,
    const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (!operands.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "unexpected operand '" + operands[0] + "'",
                            "nproc accepts flags without operands");
    return 1;
  }

  u64 ignored_count = 0;
  if (FLAG_NPROC_IGNORE.is_set()) {
    let const parsed = FLAG_NPROC_IGNORE.value().to<u64>();
    if (parsed.is_error()) {
      KOSHKIT_REPORT_ERROR_AT(
          FLAG_NPROC_IGNORE.value_location(),
          "invalid number '" +
              String{cxt.scratch_allocator(), FLAG_NPROC_IGNORE.value()} + "'",
          "use an unsigned decimal count");
      return 1;
    }
    ignored_count = parsed.value();
  }

  let const counts = os::get_processor_counts();
  let const processor_count = FLAG_NPROC_ALL.is_enabled()
                                  ? counts.configured_count
                                  : counts.online_count;
  let const result_count =
      ignored_count >= processor_count
          ? usize{1}
          : processor_count - static_cast<usize>(ignored_count);
  let output = String::from(result_count, cxt.scratch_allocator());
  output += '\n';
  ec.print_to_stdout(output);
  return 0;
}

}
