/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the killall utility. It resolves signal names or
 * numbers, enumerates processes by exact name, and signals every matching
 * process.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL(
    "[-l] [-s signal] name",
    "The killall utility sends a signal to each process by exact name.");

FLAG(KILLALL_SIGNAL, String, 's', "signal",
     "The signal to send, a name such as TERM or a number such as 15.");
FLAG(KILLALL_LIST, Bool, 'l', "list", "List the signal names and exit.");

REGISTER_KOSHKIT_UTIL_FLAGS(Killall);

namespace koshka::koshkit {

fn Killall::execute(const ExecContext &ec, EvalContext &cxt,
                    const ArrayList<String> &args,
                    const ArrayList<SourceLocation> &arg_locations) const throws
    -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (FLAG_KILLALL_LIST.is_enabled()) {
    ec.print_to_stdout(format_signal_list());
    return 0;
  }

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() != 1) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1], "expects one process name",
                            "pass one name, e.g. `killall firefox`");
    return 1;
  }

  let const wanted = operands[0].view();
  let const signal_number = resolve_koshkit_signal(
      FLAG_KILLALL_SIGNAL.is_set() ? FLAG_KILLALL_SIGNAL.value() : StringView{},
      FLAG_KILLALL_SIGNAL.value_location(), cxt.scratch_allocator());

  let const self_pid = os::get_shell_process_id();
  let const processes = os::enumerate_processes();
  bool has_signaled_any = false;
  for (let const &process : processes) {
    if (process.pid == self_pid) continue;
    if (process.name == wanted) {
      if (os::signal_process(os::process_from_pid(process.pid), signal_number))
        has_signaled_any = true;
    }
  }

  if (!has_signaled_any) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            String{cxt.scratch_allocator(), wanted} +
                                ": no process found");
  }
  return has_signaled_any ? 0 : 1;
}

} /* namespace koshka::koshkit */
