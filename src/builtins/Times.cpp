/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the times builtin. The times
 * builtin prints the user and system time the shell and its children have
 * used.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("");

HELP_DESCRIPTION_DECL(
    "The times builtin prints the user and system time the shell and its "
    "children have used.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Times);

namespace koshka {

Times::Times() = default;

pure fn Times::kind() const wontthrow -> Builtin::Kind { return Kind::Times; }

cold i32 Times::execute(ExecContext &ec, EvalContext &cxt) const throws
{
  LOG(Debug, "times printing the shell and child process accounting");

  let const times = os::read_process_cpu_times();

  let const decimal_count = cxt.runtime_state().is_posix_mode() ? 6 : 3;
  let const do_format = [&](double seconds) throws -> String {
    return utils::format_minutes_seconds(seconds, decimal_count);
  };

  let out = String{cxt.scratch_allocator()};
  out += do_format(times.self_user_seconds) + " " +
         do_format(times.self_system_seconds) + "\n";
  out += do_format(times.child_user_seconds) + " " +
         do_format(times.child_system_seconds) + "\n";
  ec.print_to_stdout(out);

  return 0;
}

} /* namespace koshka */
