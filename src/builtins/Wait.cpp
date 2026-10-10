/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file waits for every tracked job, for named job and process targets, or
 * for the next job to finish, and propagates the waited status. It reports
 * invalid targets, stores the waited process id, and marks waited jobs for the
 * job table to forget while their statuses stay known.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-fn] [-p var] [%job|pid ...]");

HELP_DESCRIPTION_DECL(
    "The wait builtin blocks until the specified jobs finish.");

FLAG(WAIT_NEXT, Bool, 'n', "",
     "Wait until the next specified job, or any job, finishes.");
FLAG(WAIT_FORCE, Bool, 'f', "",
     "Wait for each job to terminate, even if it stops first.");
FLAG(WAIT_PID_VARIABLE, String, 'p', "",
     "Store the process id whose status is returned in the variable.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Wait);

namespace koshka {

namespace {

fn find_job_by_process(JobTable &table, i64 process_id) wontthrow -> job *
{
  for (job &entry : table.jobs()) {
    if (entry.process_id == process_id) return &entry;

    for (let const process : entry.earlier_pipeline_processes)
      if (os::process_has_id(process, process_id)) return &entry;
  }

  return nullptr;
}

} /* namespace */

static fn wait_for_operands(ExecContext &ec, EvalContext &cxt) throws -> i32;

fn Wait::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const status = wait_for_operands(ec, cxt);
  cxt.release_finished_coprocess();

  return status;
}

static fn wait_for_operands(ExecContext &ec, EvalContext &cxt) throws -> i32
{
  let operand_locations = ArrayList<SourceLocation>{cxt.scratch_allocator()};
  let const args = PARSE_BUILTIN_ARGS_WITH_LOCATIONS(ec, operand_locations);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  ASSERT(!args.is_empty());

  let &table = cxt.job_table_store();
  let const should_keep_waited_statuses = !cxt.runtime_state().is_posix_mode();
  let const should_wait_for_termination = FLAG_WAIT_FORCE.is_enabled();
  let const has_pid_variable = FLAG_WAIT_PID_VARIABLE.is_set();
  let const pid_variable = FLAG_WAIT_PID_VARIABLE.value();

  if (has_pid_variable && !name_is_valid_identifier(pid_variable)) {
    report_invalid_identifier(ec, cxt, ec.source_location(), pid_variable);

    return 1;
  }

  let const do_store_pid = [&](Maybe<i64> process_id) throws -> void {
    if (!has_pid_variable) return;

    if (process_id.has_value()) {
      cxt.set_shell_variable(
          pid_variable, String::from(*process_id, cxt.scratch_allocator()));
    } else {
      cxt.unset_shell_variable(pid_variable);
    }
  };

  let const do_was_interrupted = [] wontthrow -> bool {
    return os::peek_pending_signal_besides_child() != 0;
  };

  let const do_report_unknown_target = [&](usize i) throws -> i32 {
    let const &target = args[i];

    if (!target.is_empty() && target[0] == '%') {
      report_soft_builtin_error(ec, cxt, operand_locations[i],
                                target + ": no such job",
                                "List the running jobs with `jobs`");

      return cxt.runtime_state().is_posix_mode() ? 2 : 127;
    }

    if (target.to<i64>().is_error()) {
      report_soft_builtin_error(ec, cxt, operand_locations[i],
                                "'" + target +
                                    "': not a pid or valid job spec");

      return 1;
    }

    if (!cxt.runtime_state().is_posix_mode()) {
      report_soft_builtin_error(ec, cxt, operand_locations[i],
                                "pid " + target +
                                    " is not a child of this shell",
                                "List the running jobs with `jobs -l`");
    }

    return 127;
  };

  let const do_find_target_job = [&](usize i) throws -> job * {
    let const &target = args[i];

    job *matched = nullptr;
    if (!target.is_empty() && target[0] == '%') {
      matched = table.find_job_by_spec(target);
    } else if (let const parsed = target.to<i64>(); !parsed.is_error()) {
      matched = find_job_by_process(table, parsed.value());
    }

    if (matched != nullptr && matched->is_inherited) {
      return nullptr;
    }

    return matched;
  };

  let const do_finish_next_wait = [&](next_job_wait outcome) throws -> i32 {
    if (outcome.was_interrupted) return outcome.status;

    if (!outcome.job_id.has_value()) {
      do_store_pid(None);

      return 127;
    }

    do_store_pid(outcome.process_id);
    table.forget_done_job(*outcome.job_id);

    return outcome.status;
  };

  if (FLAG_WAIT_NEXT.is_enabled()) {
    let job_ids = ArrayList<i32>{cxt.scratch_allocator()};

    if (args.count() == 1)
      return do_finish_next_wait(
          table.wait_for_next_job(job_ids, should_wait_for_termination));

    if (has_pid_variable &&
        cxt.variable_store().attributes().is_nameref(pid_variable))
    {
      cxt.variable_store().attributes().set(pid_variable,
                                            variable_attribute::Nameref, false);
      cxt.unset_shell_variable(pid_variable);
    }

    let other_pids = ArrayList<i64>{cxt.scratch_allocator()};
    let unknown_targets = ArrayList<usize>{cxt.scratch_allocator()};
    for (usize i = 1; i < args.count(); i++) {
      if (job *const matched = do_find_target_job(i); matched != nullptr) {
        job_ids.push(matched->id);

        continue;
      }

      let const parsed = args[i].to<i64>();
      if (args[i].is_empty() || args[i][0] == '%' || parsed.is_error()) {
        unknown_targets.push(i);

        continue;
      }

      if (let const status = table.find_finished_status(parsed.value());
          status.has_value())
      {
        do_store_pid(parsed.value());

        return *status;
      }

      if (cxt.is_pending_process_substitution(parsed.value())) {
        other_pids.push(parsed.value());
      } else {
        unknown_targets.push(i);
      }
    }

    for (let const i : unknown_targets) {
      let const &target = args[i];
      if (!target.is_empty() && target[0] == '%') {
        unused(do_report_unknown_target(i));
      } else {
        report_soft_builtin_error(ec, cxt, operand_locations[i],
                                  "'" + target +
                                      "': not a pid or valid job spec");
      }
    }

    let const do_has_done_target_job = [&]() throws -> bool {
      table.update_jobs();
      for (let const &entry : table.jobs()) {
        if (entry.state == job::State::Done && !entry.was_waited &&
            job_ids.find(entry.id).has_value())
        {
          return true;
        }
      }

      return false;
    };

    while (!job_ids.is_empty() && !other_pids.is_empty()) {
      for (let const process_id : other_pids) {
        if (let const status =
                cxt.wait_for_process_substitution(process_id, false);
            status.has_value())
        {
          do_store_pid(process_id);

          return *status;
        }
      }

      if (do_has_done_target_job()) break;

      if (let const number = os::peek_pending_signal_besides_child();
          number != 0)
      {
        return 128 + number;
      }

      if constexpr (os::HAS_CHILD_STATE_CHANGE_WAIT)
        os::wait_for_child_state_change();
      else
        os::sleep_for_seconds(0.005);
    }

    if (!job_ids.is_empty())
      return do_finish_next_wait(
          table.wait_for_next_job(job_ids, should_wait_for_termination));

    for (let const process_id : other_pids) {
      if (let const status = cxt.wait_for_process_substitution(process_id);
          status.has_value())
      {
        do_store_pid(process_id);

        return *status;
      }
    }

    do_store_pid(None);

    return 127;
  }

  if (args.count() == 1) {
    LOG(Debug, "wait blocking on every job of %zu", table.jobs().count());

    do_store_pid(None);

    let finished_before_ids = ArrayList<i32>{heap_allocator()};
    if (should_keep_waited_statuses) {
      table.update_jobs();

      for (let const &job : table.jobs())
        if (job.state == job::State::Done && !job.is_inherited) {
          finished_before_ids.push(job.id);
        }
    }

    for (job &job : table.jobs()) {
      if (job.is_inherited) continue;

      let const status = table.wait_for_job_processes(
          job, nullptr, should_wait_for_termination);

      if (do_was_interrupted()) {
        table.forget_done_jobs();

        return status;
      }
    }

    let const &last_background_pid = table.last_background_pid();
    if (last_background_pid.has_value())
      unused(cxt.wait_for_process_substitution(*last_background_pid));

    for (let const &waited : table.jobs())
      if (!finished_before_ids.find(waited.id).has_value()) {
        table.mark_job_waited(waited.id, false);
      }

    if (!should_keep_waited_statuses) return 0;

    table.forget_waited_jobs();
    table.forget_finished_statuses();

    return 0;
  }

  i32 status = 0;
  for (usize i = 1; i < args.count(); i++) {
    LOG(Debug, "wait blocking on target '%s'", args[i].c_str());

    if (job *const matched = do_find_target_job(i); matched != nullptr) {
      let const job_id = matched->id;
      let const process_id = matched->process_id;

      status = table.wait_for_job_processes(*matched, nullptr,
                                            should_wait_for_termination);
      do_store_pid(process_id);
      table.mark_job_waited(job_id, should_keep_waited_statuses);
    } else if (let const parsed = args[i].to<i64>();
               args[i].is_empty() || args[i][0] == '%' || parsed.is_error())
    {
      status = do_report_unknown_target(i);
      do_store_pid(None);
    } else if (let const known_status =
                   cxt.wait_for_process_substitution(parsed.value());
               known_status.has_value())
    {
      status = *known_status;
      do_store_pid(parsed.value());
    } else {
      status = do_report_unknown_target(i);
      do_store_pid(None);
    }

    if (do_was_interrupted()) return status;
  }

  return status;
}

} /* namespace koshka */
