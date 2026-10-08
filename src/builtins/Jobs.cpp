/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the jobs builtin. The jobs
 * builtin lists the background jobs and their state.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../CLIColors.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[-lnprs] [jobspec ...]");
HELP_DESCRIPTION_DECL(
    "The jobs builtin lists the background jobs and their state.");

FLAG(JOBS_LONG, Bool, 'l', "",
     "List the process id in addition to the normal information.");
FLAG(JOBS_PIDS, Bool, 'p', "", "List process ids only.");
FLAG(JOBS_RUNNING, Bool, 'r', "", "Restrict the output to running jobs.");
FLAG(JOBS_STOPPED, Bool, 's', "", "Restrict the output to stopped jobs.");
FLAG(JOBS_CHANGED, Bool, 'n', "",
     "List only jobs that changed state since the last notification.");
FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Jobs);

namespace koshka {

namespace {

enum class jobs_color_mode : u8
{
  Plain,
  Colored,
};

fn state_color(job::State state, jobs_color_mode color_mode) throws
    -> StringView
{
  if (color_mode == jobs_color_mode::Plain) return StringView{};
  switch (state) {
  case job::State::Running: return colors::ansi::BOLD_GREEN;
  case job::State::Stopped: return colors::ansi::BOLD_YELLOW;
  case job::State::Done: return colors::ansi::DIM;
  }
  return StringView{};
}

fn should_color_jobs(EvalContext &cxt) throws -> bool
{
  return cxt.execution_store().shell_is_interactive() &&
         colors::stdout_wants_color();
}

} /* namespace */

fn Jobs::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const names = PARSE_BUILTIN_ARGS(ec);

  if (FLAG_HELP.is_enabled()) SHOW_BUILTIN_HELP_AND_RETURN(ec);

  cxt.job_table_store().forget_waited_jobs();
  cxt.job_table_store().update_jobs();
  cxt.release_finished_coprocess();

  let const color_mode = should_color_jobs(cxt) ? jobs_color_mode::Colored
                                                : jobs_color_mode::Plain;
  let &jobs = cxt.job_table_store().jobs();

  LOG(Debug, "jobs listing %zu registered jobs", jobs.count());

  let selected = ArrayList<usize>{cxt.scratch_allocator()};
  i32 status = 0;
  if (names.count() > 1) {
    for (usize a = 1; a < names.count(); a++) {
      if (let const index =
              cxt.job_table_store().find_job_index_by_spec(names[a].view());
          index.has_value())
      {
        selected.push(*index);
      } else {
        report_soft_builtin_error(ec, cxt,
                                  "Unable to list the job '" + names[a] +
                                      "' because no such job exists");
        status = cxt.runtime_state().is_posix_mode() ? 2 : 1;
      }
    }
  } else {
    for (usize i = 0; i < jobs.count(); i++)
      selected.push(i);
  }

  let out = String{cxt.scratch_allocator()};
  for (let index : selected) {
    job &job = jobs[index];

    if (FLAG_JOBS_RUNNING.is_enabled() && job.state != job::State::Running) {
      continue;
    }
    if (FLAG_JOBS_STOPPED.is_enabled() && job.state != job::State::Stopped) {
      continue;
    }
    if (FLAG_JOBS_CHANGED.is_enabled() && !job.has_unreported_state_change) {
      continue;
    }

    job.has_unreported_state_change = false;

    if (FLAG_JOBS_PIDS.is_enabled()) {
      out += String::from(job.process_id, cxt.scratch_allocator());
      out.push('\n');
      continue;
    }

    cxt.job_table_store().append_status_line(
        out, index,
        job_line_format{.should_show_process_id = FLAG_JOBS_LONG.is_enabled(),
                        .is_posix = cxt.runtime_state().is_posix_option_on(),
                        .state_color = state_color(job.state, color_mode),
                        .color_reset = colors::ansi::RESET});
    out.push('\n');
  }
  ec.print_to_stdout(out);

  cxt.job_table_store().forget_done_jobs();
  return status;
}

} /* namespace koshka */
