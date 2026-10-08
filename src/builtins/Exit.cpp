/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the exit builtin. The exit
 * builtin ends the shell with a status.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL("The exit builtin ends the shell with a status.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Exit);

namespace koshka {

fn Exit::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  let status = static_cast<i64>(cxt.execution_store().last_exit_status());

  if (let const trap_status = cxt.trap_store().action_frame().saved_exit_status;
      trap_status.has_value())
  {
    status = static_cast<i64>(*trap_status);
  }

  return finish_exit_builtin(ec, cxt, status, {},
                             cxt.runtime_state().is_posix_mode()
                                 ? StringView{}
                                 : StringView{"too many arguments"},
                             "exit takes at most one status, e.g. `exit 1`");
}

}
