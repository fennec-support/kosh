/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the pkill utility. It selects processes by a name
 * pattern and a session list, optionally inverts the selection, and signals
 * each selected process.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Utils.hpp"

KOSHKIT_UTIL_DECL(
    "[-l] [-SIGNAL] [--signal signal] [-s sid,...] [--inverse] [pattern]",
    "The pkill utility sends a signal to each process whose name matches a "
    "pattern.");

FLAG(PKILL_SIGNAL,
     String,
     '\0',
     "signal",
     "The signal to send, a name such as TERM or a number such as 15.");
FLAG(PKILL_SESSION,
     String,
     's',
     "session",
     "Select processes in the listed sessions, where 0 is the own session.");
FLAG(PKILL_INVERSE, Bool, '\0', "inverse", "Signal the unselected processes.");
FLAG(PKILL_LIST, Bool, 'l', "list", "List the signals and exit.");

REGISTER_KOSHKIT_UTIL_FLAGS(Pkill);

namespace koshka::koshkit {

static fn
is_signal_spelling(StringView spelled, Allocator allocator) throws
    -> bool
{
  if (spelled.is_all_decimal_digits()) return true;

  let uppercased = String{allocator, spelled};

  uppercased.uppercase_ascii();

  return os::signal_number_from_name(uppercased.view()).has_value();
}

fn
Pkill::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  let filtered_args      = ArrayList<String>{cxt.scratch_allocator()};
  let filtered_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  StringView     dash_signal{};
  SourceLocation dash_signal_location{};
  bool           has_dash_signal    = false;
  bool           is_after_separator = false;

  for (usize index = 0; index < args.count(); index++) {
    let const argument = args[index].view();

    if (argument == "--") is_after_separator = true;

    let const is_dash_signal =
        index > 0 && !is_after_separator && !has_dash_signal &&
        argument.length > 1 && argument[0] == '-' && argument[1] != '-' &&
        is_signal_spelling(argument.substring(1), cxt.scratch_allocator());

    if (is_dash_signal) {
      dash_signal          = argument.substring(1);
      dash_signal_location = arg_locations[index];
      has_dash_signal      = true;
      continue;
    }

    filtered_args.push(String{cxt.scratch_allocator(), argument});
    filtered_locations.push(arg_locations[index]);
  }

  KOSHKIT_PARSE_OPERANDS_OR_HELP(filtered_args, filtered_locations);

  if (FLAG_PKILL_LIST.is_enabled()) {
    ec.print_to_stdout(format_signal_list());

    return 0;
  }

  if (has_dash_signal && FLAG_PKILL_SIGNAL.is_set()) {
    KOSHKIT_REPORT_ERROR_AT(FLAG_PKILL_SIGNAL.value_location(),
                            "The signal is given twice",
                            "Pass either -SIGNAL or --signal, e.g. "
                            "`pkill -TERM ssh`");

    return 1;
  }

  if (operands.count() > 1) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1], "Expects one pattern",
                            "Pass one pattern, e.g. `pkill ssh`");

    return 1;
  }

  let const has_pattern = !operands.is_empty();

  if (!has_pattern && !FLAG_PKILL_SESSION.is_set())
    return report_usage_error(ec, cxt, args[0].view());

  let const pattern = has_pattern ? operands[0].view() : StringView{};

  if (has_pattern && pattern.is_empty()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "Requires a non-empty pattern",
                            "Pass a pattern, e.g. `pkill ssh`");

    return 1;
  }

  let sessions = ArrayList<i64>{cxt.scratch_allocator()};

  if (FLAG_PKILL_SESSION.is_set()) {
    let const list        = FLAG_PKILL_SESSION.value();
    usize     field_start = 0;

    for (usize position = 0; position <= list.length; position++) {
      if (position != list.length && list[position] != ',') continue;

      let const field =
          list.substring_of_length(field_start, position - field_start);
      let const parsed = field.to<i64>();

      if (!field.is_all_decimal_digits() || parsed.is_error()) {
        KOSHKIT_REPORT_ERROR_AT(FLAG_PKILL_SESSION.value_location(),
                                "Invalid session id '" +
                                    String{cxt.scratch_allocator(), field} +
                                    "'",
                                "Pass session ids separated by commas, e.g. "
                                "`pkill -s 0,1 ssh`");

        return 1;
      }

      sessions.push(parsed.value() == 0 ? os::get_current_session_id()
                                        : parsed.value());
      field_start = position + 1;
    }
  }

  let const signal_number =
      has_dash_signal
          ? resolve_koshkit_signal(dash_signal, dash_signal_location,
                                   cxt.scratch_allocator())
          : resolve_koshkit_signal(FLAG_PKILL_SIGNAL.value(),
                                   FLAG_PKILL_SIGNAL.value_location(),
                                   cxt.scratch_allocator());

  let const should_invert  = FLAG_PKILL_INVERSE.is_enabled();
  let const self_pid       = os::get_shell_process_id();
  let const current_pid    = os::get_current_process_id();
  let const processes      = os::enumerate_processes();
  bool      did_signal_any = false;

  for (let const &process : processes) {
    /* pid 0 is the kernel on macOS, and kill(0) signals the caller's group. */
    if (process.pid <= 0 || process.pid == self_pid ||
        process.pid == current_pid)
    {
      continue;
    }

    let const does_name_match =
        !has_pattern || process.name.find_substring(pattern, 0).has_value();
    let const does_session_match =
        sessions.is_empty() || sessions.find(process.session_id).has_value();

    if ((does_name_match && does_session_match) == should_invert) continue;

    if (os::signal_process(os::process_from_pid(process.pid), signal_number)) {
      did_signal_any = true;
    } else {
      let const reason = os::last_system_error_message();

      report_soft_koshkit_error(
          ec, cxt,
          "Killing pid " + String::from(process.pid, cxt.scratch_allocator()) +
              " failed: " + reason);
    }
  }

  return did_signal_any ? 0 : 1;
}

} /* namespace koshka::koshkit */
