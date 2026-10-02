/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the compopt builtin. When the
 * filenames option is set inside a running completion function, directory
 * candidates of the current completion are given a trailing slash. Other
 * options and named commands are accepted with no effect.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-o option] [-DEI] [+o option] [name ...]");
HELP_DESCRIPTION_DECL(
    "When -o filenames is given inside a completion function, directory "
    "candidates are given a trailing slash, and +o filenames turns the slash "
    "off again. Other options are accepted with no effect.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Compopt);

namespace koshka {

Compopt::Compopt() = default;

pure fn Compopt::kind() const wontthrow -> Builtin::Kind
{
  return Kind::Compopt;
}

fn Compopt::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && args[1] == "--help") {
    SHOW_BUILTIN_HELP_AND_RETURN(ec);
  }

  if (!cxt.execution_store().completion_function_running()) {
    LOG(Debug, "compopt accepting %zu arguments outside completion",
        args.count() - 1);
    return 0;
  }

  Maybe<bool> should_mark_directories = None;
  for (usize i = 1; i < args.count(); i++) {
    let const argument = args[i].view();
    if (argument != "-o" && argument != "+o") {
      LOG(Debug, "compopt leaving the current completion for '%.*s'",
          static_cast<int>(argument.length), argument.data);
      return 0;
    }

    if (i + 1 >= args.count()) break;

    i++;
    if (args[i].view() == "filenames")
      should_mark_directories = argument == "-o";
  }

  if (should_mark_directories.has_value()) {
    LOG(Debug, "compopt %s directory marks for the current completion",
        *should_mark_directories ? "enabling" : "disabling");
    cxt.execution_store().should_mark_completion_directories() =
        *should_mark_directories;
  }

  return 0;
}

} /* namespace koshka */
