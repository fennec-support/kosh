/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the killall utility. It resolves signal spellings or
 * numbers, enumerates processes by exact name, and signals every matching
 * process.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"

KOSHKIT_UTIL_DECL(
    "[-l] [-s signal] name",
    "The killall utility can send a signal to each process by exact name.");

FLAG(KILLALL_SIGNAL,
     String,
     's',
     "signal",
     "The signal to send, a name such as TERM or a number such as 15.");
FLAG(KILLALL_LIST, Bool, 'l', "list", "List the signals and exit.");

REGISTER_KOSHKIT_UTIL_FLAGS(Killall);

namespace koshka::koshkit {

/* memory: the process list from enumerate_processes() is an ArrayList that
           this call frees on return.
   time:   O(processes) to enumerate, then one os::signal_process() call per
           process whose name matches exactly. */
fn
Killall::execute(const ExecContext               &ec,
                 EvalContext                     &cxt,
                 const ArrayList<String>         &args,
                 const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (FLAG_KILLALL_LIST.is_enabled()) {
    ec.print_to_stdout(format_signal_list());

    return 0;
  }

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());
  if (operands.count() != 1) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1], "Expects one process name",
                            "Pass one name, e.g. `killall firefox`");

    return 1;
  }

  /* at this point: operands holds exactly one process name, and no signal
     list was requested. */
  let const wanted        = operands[0].view();
  let const signal_number = resolve_koshkit_signal(
      FLAG_KILLALL_SIGNAL.is_set() ? FLAG_KILLALL_SIGNAL.value() : StringView{},
      FLAG_KILLALL_SIGNAL.value_location());

  let const self_pid         = os::get_shell_process_id();
  let const current_pid      = os::get_current_process_id();
  let const processes        = os::enumerate_processes();
  bool      has_matched_any  = false;
  bool      has_signaled_any = false;

  for (let const &process : processes) {
    /* pid 0 is the kernel on macOS, and kill(0) signals the caller's group. */
    if (process.pid <= 0 || process.pid == self_pid ||
        process.pid == current_pid)
    {
      continue;
    }

    if (process.name == wanted) {
      has_matched_any = true;
      if (os::signal_process(os::process_from_pid(process.pid), signal_number))
      {
        has_signaled_any = true;
      } else {
        let const reason   = os::last_system_error_message();
        let const pid_text = String::from(process.pid, cxt.scratch_allocator());

        KOSHKIT_REPORT_ERROR_AT(operand_locations[0], "Killing pid " +
                                                          pid_text +
                                                          " failed: " + reason);
      }
    }
  }

  if (!has_matched_any) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            String{cxt.scratch_allocator(), wanted} +
                                ": no process found");
  }

  return has_signaled_any ? 0 : 1;
}

} /* namespace koshka::koshkit */
