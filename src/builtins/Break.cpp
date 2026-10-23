/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the break builtin, which exits an enclosing loop.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL("An enclosing loop can be exited.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Break);

namespace koshka {

fn
Break::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  let const is_posix_mode = cxt.runtime_state().is_posix_mode();

  if (cxt.execution_store().loop_depth() == 0 && !is_posix_mode) {
    LOG(All, "break outside a loop does nothing");
    report_loop_control_without_loop(ec, cxt);

    return 0;
  }

  i64 level = 1;

  if (ec.args().count() > 1) {
    let const parsed_level = ec.args()[1].to<i64>();

    if (parsed_level.is_error()) {
      let message = String{cxt.scratch_allocator()};

      message += "Unable to break because '";
      message.append(ec.args()[1].view());
      message += "' is not a valid integer";

      ErrorWithLocation located{ec.arg_location_at(1), message.view()};

      located.set_script_fatal();
      located.set_command_status(2);
      throw located;
    }

    level = parsed_level.value();
  }

  if (ec.args().count() > 2 && !is_posix_mode) {
    ErrorWithLocation located{ec.arg_location_at(2),
                              StringView{"At most one loop count is accepted"}};

    located.set_script_fatal();
    located.set_command_status(1);
    throw located;
  }

  if (level < 1 && is_posix_mode) {
    let error = make_error_for_arg(ec, 1,
                                   "Unable to break because '" + ec.args()[1] +
                                       "' is not a valid loop count");

    error.set_command_status(2);
    throw error;
  }

  if (level < 1) {
    report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                              "The count '" + ec.args()[1] +
                                  "' is out of range");

    /* a bash mood leaves every enclosing loop for a count below one. */
    if (cxt.runtime_state().is_bash_compatible()) {
      cxt.request_break(static_cast<i64>(cxt.execution_store().loop_depth()),
                        ec.source_location());
    } else {
      cxt.request_break(1, ec.source_location());
    }

    return 1;
  }

  if (cxt.execution_store().loop_depth() == 0) return 0;

  /* at this point: level is at least 1, and an enclosing loop exists. */
  LOG(All, "break leaving %lld enclosing loops", static_cast<long long>(level));
  cxt.request_break(level, ec.source_location());

  return 0;
}

} /* namespace koshka */
