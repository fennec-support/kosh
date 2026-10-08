/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the eval builtin. The eval
 * builtin runs its arguments as a command in the current shell.
 */

#include "../Eval.hpp"

#include "../Builtin.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[arg ...]");

HELP_DESCRIPTION_DECL(
    "The eval builtin runs its arguments as a command in the current shell.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Eval);

namespace koshka {

fn Eval::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  usize first = 1;
  if (ec.args().count() > 1) {
    let const &lead = ec.args()[1];
    if (lead == "--") {
      first = 2;
    } else if (lead.length() >= 2 && lead[0] == '-') {
      let invalid_option = String{cxt.scratch_allocator()};
      invalid_option += lead[0];
      invalid_option.append(lead.view().substring_of_length(
          1, utils::decode_utf8(lead.view(), 1, 0).length));

      let note = String{cxt.scratch_allocator()};
      note += "Try `";
      note.append(ec.program().view());
      note += " --help` for more info";

      report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                                invalid_option + ": invalid option",
                                note.view());
      return 2;
    }
  }

  let joined = String{cxt.scratch_allocator()};
  for (usize i = first; i < ec.args().count(); i++) {
    if (i > first) joined += ' ';
    joined.append(ec.args()[i].view());
  }

  if (joined.is_empty()) return 0;

  LOG(Debug, "eval running %zu joined bytes in the current shell",
      joined.length());

  return cxt.run_source(
      joined, "eval", ec.source_location(), StringView{"eval"}, nullptr,
      nullptr, return_handling::Propagate, history_recording::Disabled, nullptr,
      ec.is_called_through_command ? syntax_error_reach::Command
                                   : syntax_error_reach::PosixScript);
}

} /* namespace koshka */
