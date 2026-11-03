/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the exit builtin, which ends the shell with a status.
 */

#include "../Builtin.hpp"
#include "../Eval.hpp"
#include "../Toiletline.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL("The shell can be ended with a status.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Exit);

namespace koshka {

fn
Exit::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  let status = static_cast<i64>(cxt.execution_store().last_exit_status());

  if (let const trap_status = cxt.trap_store().action_frame().saved_exit_status;
      trap_status.has_value())
  {
    status = static_cast<i64>(*trap_status);
  }

  let has_sourced_file_frame = false;

  for (let const &frame : cxt.source_store().source_frames())
    if (frame.has_bash_source_row()) has_sourced_file_frame = true;

  /* an interactive shell that takes commands from -c or standard input
     announces the exit outside sourced files and traps. The line editor
     shows KOSH_FAREWELL instead. */
  let const should_announce =
      cxt.execution_store().shell_is_interactive() && !cxt.in_subshell() &&
      (cxt.execution_store().has_execution_string() ||
       cxt.execution_store().is_reading_standard_input()) &&
      !has_sourced_file_frame &&
      cxt.source_store().rejected_return_source_frames() == 0 &&
      !toiletline::is_active();

  if (should_announce) {
    ec.print_to_stderr(cxt.startup_store().is_login_shell() ? "logout\n"
                                                            : "exit\n");
  }

  return finish_exit_builtin(ec, cxt, status, {},
                             cxt.runtime_state().is_posix_mode()
                                 ? StringView{}
                                 : StringView{"Too many arguments"},
                             "At most one status is accepted, e.g. "
                             "`exit 1`");
}

} /* namespace koshka */
