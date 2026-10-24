/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the kill builtin, which sends a signal to a job or
 * process.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../base/Trace.hpp"
#include "../platform/Platform.hpp"
#include "../util/Utils.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-l] [-signal] %job|pid [...]");
HELP_DESCRIPTION_DECL("A signal can be sent to a job or process.");

FLAG(KILL_LIST, Bool, 'l', "list", "List the signals and exit.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Kill);

namespace koshka {

/* memory: the --list output and signal_error are allocated in
           scratch_allocator() and are released when the scratch arena is reset.
   time:   one signal_process() call per target, plus one per earlier pipeline
           stage of each %job target. forget_waited_jobs() runs once. */
fn
Kill::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  if (args.count() > 1 && (args[1] == "-l" || args[1] == "--list")) {
    if (args.count() == 2) {
      ec.print_to_stdout(koshkit::format_signal_list());

      return 0;
    }

    let listing = String{cxt.scratch_allocator()};
    i32 status  = 0;

    for (usize i = 2; i < args.count(); i++) {
      let const    &spec = args[i];
      Maybe<String> name = koshka::None;

      /* 0 and EXIT are the exit trap. */
      if (spec == "0") {
        name = String{cxt.scratch_allocator(), "EXIT"};
      } else if (utils::equals_ascii_ignoring_case(spec, "EXIT")) {
        name = String{cxt.scratch_allocator(), "0"};
      } else if (StringView{spec}.is_all_decimal_digits()) {
        let const parsed = StringView{spec}.to<i64>();

        if (!parsed.is_error() && parsed.value() <= INT32_MAX) {
          let const number = static_cast<i32>(parsed.value());

          name = os::signal_name_from_number(number);
          if (!name.has_value() && number > 128) {
            name = os::signal_name_from_number(number - 128);
          }
        }
      } else if (let const number = os::signal_number_from_name(spec);
                 number.has_value())
      {
        name = String::from(*number, cxt.scratch_allocator());
      }

      if (name.has_value()) {
        listing += *name;
        listing += '\n';
      } else {
        report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                  StringView{"'"} + spec +
                                      "' is not a valid signal");
        status = 1;
      }
    }

    ec.print_to_stdout(listing);

    return status;
  }

  usize first_target  = 1;
  let   signal_number = os::signal_number_from_name("TERM").value_or(15);

  let const do_resolve_signal = [&ec](StringView spec,
                                      usize      index) throws -> i32 {
    if (let const parsed = spec.to<i64>(); !parsed.is_error() &&
                                           spec[0] >= '0' && spec[0] <= '9' &&
                                           parsed.value() <= INT32_MAX)
    {
      return static_cast<i32>(parsed.value());
    }

    if (let const resolved = os::signal_number_from_name(spec);
        resolved.has_value())
    {
      return *resolved;
    }

    throw make_error_for_arg(
        ec, index, StringView{"'"} + spec + "' is not a valid signal");
  };

  if (args.count() > 1) {
    if (args[1] == "--") {
      first_target = 2;
    } else if (args[1] == "-s" || args[1] == "-n") {
      if (args.count() < 3) {
        return report_usage_error(ec, cxt, ec.program());
      }

      signal_number = do_resolve_signal(args[2], 2);
      first_target  = 3;
    } else if (args[1].length() > 2 && args[1].starts_with("-s")) {
      /* a signal attached to the option, as in -sTERM, is accepted. */
      signal_number = do_resolve_signal(args[1].substring(2), 1);
      first_target  = 2;
    } else if (args[1].length() > 1 && args[1][0] == '-') {
      signal_number = do_resolve_signal(args[1].substring(1), 1);
      first_target  = 2;
    }

    if (first_target > 1 && first_target < args.count() &&
        args[first_target] == "--" && args[1] != "--")
    {
      first_target++;
    }
  }

  if (first_target >= args.count())
    return report_usage_error(ec, cxt, ec.program());

  /* at this point: first_target indexes an operand in args, and
     signal_number holds the resolved signal. */
  cxt.job_table_store().forget_waited_jobs();

  i32 status = 0;

  for (usize i = first_target; i < args.count(); i++) {
    const String &target = args[i];

    if (!target.is_empty() && target[0] == '%') {
      const job *const job = cxt.job_table_store().find_job_by_spec(target);

      if (job == nullptr) {
        report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                  StringView{"'"} + target +
                                      "' is not a known job");
        status = 1;
        continue;
      }

      let did_signal_process = false;
      let signal_error       = String{cxt.scratch_allocator()};

      if (job->is_primary_process_active) {
        if (os::signal_process(job->pid, signal_number))
          did_signal_process = true;
        else
          signal_error = os::last_system_error_message();
      }

      for (let const process : job->earlier_pipeline_processes) {
        if (os::signal_process(process, signal_number))
          did_signal_process = true;
        else
          signal_error = os::last_system_error_message();
      }

      if (!did_signal_process) {
        let const did_find_active_process =
            job->is_primary_process_active ||
            !job->earlier_pipeline_processes.is_empty();
        let const reason = did_find_active_process
                               ? signal_error.view()
                               : StringView{"The job has finished"};

        report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                  StringView{"Cannot signal '"} + target +
                                      "': " + reason);
        status = 1;
      }

      continue;
    }

    const ErrorOr<i64> parsed_value = target.to<i64>();

    if (parsed_value.is_error() || parsed_value.value() > INT32_MAX ||
        parsed_value.value() < INT32_MIN)
    {
      report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                StringView{"'"} + target +
                                    "' is not a valid job or process id");
      status = 1;
      continue;
    }

    let const pid = os::process_from_pid(parsed_value.value());

    LOG(Debug, "kill sending signal %d to target '%s'", signal_number,
        target.c_str());
    if (!os::signal_process(pid, signal_number)) {
      let const signal_error = os::last_system_error_message();

      report_soft_builtin_error(ec, cxt, ec.arg_location_at(i),
                                StringView{"Cannot signal '"} + target +
                                    "': " + signal_error);
      status = 1;
    }
  }

  return status;
}

} /* namespace koshka */
