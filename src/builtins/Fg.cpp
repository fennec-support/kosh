/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the fg builtin. The fg builtin
 * brings a job to the foreground and waits for it.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Toiletline.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[%job]");
HELP_DESCRIPTION_DECL(
    "The fg builtin brings a job to the foreground and waits for it.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Fg);

namespace koshka {

Fg::Fg() = default;

pure fn Fg::kind() const wontthrow -> Builtin::Kind { return Kind::Fg; }

fn Fg::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  cxt.job_table_store().forget_waited_jobs();

  job *job = nullptr;
  if (args.count() > 1 && !args[1].is_empty()) {
    job = cxt.job_table_store().find_job_by_spec(args[1]);
    if (job == nullptr)
      throw ErrorWithDetails{"'" + args[1] + "' is not a valid job",
                             "Use a job spec like `%1`, `%+`, or `%name`"};
  } else {
    job = cxt.job_table_store().most_recent_job();
  }

  if (job == nullptr)
    throw ErrorWithDetails{"There is no such job", "List jobs with `jobs`"};

  LOG(Info, "fg bringing job %d to the foreground", job->id);

  /* A job reaped by a prior poll has its status recorded, so it is reported
     without waiting on a pid that no longer exists. */
  if (job->state == job::State::Done) {
    let const done_status = job->last_status;
    cxt.job_table_store().forget_done_jobs();
    return done_status;
  }

  let const command = job->command.view();
  ec.print_to_stdout(command + "\n");
  if (cxt.execution_store().shell_is_interactive())
    toiletline::set_title(command);

  let const should_reclaim = cxt.execution_store().shell_is_interactive() &&
                             os::shell_has_controlling_terminal();
  let const should_reclaim_after_wait =
      should_reclaim && job->process_group_id > 0;
  let const do_resume_job = [&]() throws {
    if (job->state == job::State::Stopped) continue_job(*job);
  };

  let was_stopped = false;
  i32 status;
  if (should_reclaim_after_wait) {
    LOG(Debug,
        "fg will give the terminal to process group %lld before it resumes job "
        "%d",
        static_cast<long long>(job->process_group_id), job->id);
    os::give_controlling_terminal_to_process_group(job->process_group_id);
    defer { os::reclaim_controlling_terminal(); };
    do_resume_job();
    status = cxt.job_table_store().wait_for_job_processes(*job, &was_stopped);
  } else {
    do_resume_job();
    status = cxt.job_table_store().wait_for_job_processes(*job, &was_stopped);
  }

  if (was_stopped) {
    job->state = job::State::Stopped;
    job->stopped_status = status;
    cxt.job_table_store().notify_stopped_job(job->id);
    return status;
  }

  if (let const interrupt = os::signal_number_from_name("INT");
      should_reclaim && interrupt.has_value() && status == 128 + *interrupt)
  {
    print("\n");
  }

  cxt.job_table_store().forget_done_jobs();

  return status;
}

} /* namespace koshka */
