/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the less utility. It pages the input in a full-screen
 * view with forward and backward movement and searches on a terminal, and
 * copies the input unchanged when the output is not a terminal.
 */

#include "../CLI.hpp"
#include "../CLILive.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../platform/Platform.hpp"

KOSHKIT_UTIL_DECL("[-iNS] [file ...]",
                  "Display text in a scrollable full-screen view.");

FLAG(LESS_CASE, Bool, 'i', "ignore-case", "Ignore case in searches.");
FLAG(LESS_NUMBERS,
     Bool,
     'N',
     "LINE-NUMBERS",
     "Show the line number before each line.");
FLAG(LESS_CHOP,
     Bool,
     'S',
     "chop-long-lines",
     "Cut long lines at the screen edge instead of wrapping them.");

REGISTER_KOSHKIT_UTIL_FLAGS(Less);

namespace koshka::koshkit {

fn
Less::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty() && os::is_fd_a_tty(ec.out_fd.value_or(KOSH_STDOUT)) &&
      os::is_fd_a_tty(ec.in_fd.value_or(KOSH_STDIN)))
  {
    return report_usage_error(ec, cxt, args[0].view());
  }

  let const sources =
      source_list_from_operands(operands, cxt.scratch_allocator());
  pager_options options{};

  options.is_full_screen      = true;
  options.should_show_headers = sources.count() > 1;
  options.should_number_lines = FLAG_LESS_NUMBERS.is_enabled();
  options.should_chop_lines   = FLAG_LESS_CHOP.is_enabled();
  options.should_ignore_case  = FLAG_LESS_CASE.is_enabled();

  return run_pager(ec, cxt, sources, options);
}

} /* namespace koshka::koshkit */
