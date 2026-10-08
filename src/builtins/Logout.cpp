/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the logout builtin. The logout
 * builtin ends a login shell with a status.
 */

#include "../Builtin.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[n]");

HELP_DESCRIPTION_DECL("The logout builtin ends a login shell with a status.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Logout);

namespace koshka {

fn Logout::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  ASSERT(!ec.args().is_empty());

  if (!cxt.startup_store().is_login_shell()) {
    report_soft_builtin_error(ec, cxt, ec.source_location(),
                              "Cannot use 'logout' in a non-login shell",
                              "`exit` may be used instead");
    return 1;
  }

  return finish_exit_builtin(
      ec, cxt, static_cast<i64>(cxt.execution_store().last_exit_status()),
      "The status must be a whole number such as `logout 1`",
      "Too many arguments",
      "Logout takes at most one status, such as `logout 1`");
}

}
