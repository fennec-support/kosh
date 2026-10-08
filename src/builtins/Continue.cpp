/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the continue builtin. The
 * continue builtin skips to the next iteration of an enclosing loop.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL(
    "The continue builtin skips to the next iteration of an enclosing loop.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Continue);

namespace koshka {

fn Continue::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (cxt.execution_store().loop_depth() == 0 &&
      !cxt.runtime_state().is_posix_mode())
  {
    LOG(All, "continue outside a loop does nothing");
    report_loop_control_without_loop(ec, cxt);
    return 0;
  }

  i64 level = 1;
  if (ec.args().count() > 1) {
    let const parsed_level = ec.args()[1].to<i64>();

    if (parsed_level.is_error()) {
      if (!cxt.runtime_state().is_bash_compatible()) throw parsed_level.error();

      LOG(All, "continue rejecting a non-numeric count under bash mood");
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                                "'" + ec.args()[1] +
                                    "' is not a valid loop count");

      if (!cxt.execution_store().shell_is_interactive()) {
        if (cxt.in_subshell()) {
          cxt.request_exit(2, ec.source_location());
          return 2;
        }

        cxt.run_exit_trap(2);
        utils::quit(2, utils::farewell_policy::Goodbye);
      }

      cxt.request_break(static_cast<i64>(cxt.execution_store().loop_depth()),
                        ec.source_location());
      return 2;
    }

    level = parsed_level.value();
  }

  if (level < 1) {
    if (!cxt.runtime_state().is_bash_compatible()) {
      let error =
          make_error_for_arg(ec, 1,
                             "Unable to continue because '" + ec.args()[1] +
                                 "' is not a valid loop count");
      if (cxt.runtime_state().is_posix_mode()) error.set_command_status(2);
      throw error;
    }

    LOG(All, "continue abandoning every enclosing loop for a count below one");
    report_soft_builtin_error(ec, cxt, ec.arg_location_at(1),
                              "'" + ec.args()[1] +
                                  "' is not a valid loop count");
    cxt.request_break(static_cast<i64>(cxt.execution_store().loop_depth()),
                      ec.source_location());
    return 1;
  }

  if (cxt.execution_store().loop_depth() == 0) return 0;

  LOG(All, "continue skipping to the next iteration of %lld loops",
      static_cast<long long>(level));
  cxt.request_continue(level, ec.source_location());
  return 0;
}

}
