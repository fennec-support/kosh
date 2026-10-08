/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements and is responsible for the bg builtin. The bg builtin
 * resumes a stopped job in the background.
 */

#include "../Builtin.hpp"
#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Platform.hpp"
#include "../Utils.hpp"
#include "../base/Trace.hpp"

FLAG_LIST_DECL();

HELP_SYNOPSIS_DECL("[%job]");

HELP_DESCRIPTION_DECL(
    "The bg builtin resumes a stopped job in the background.");

FLAG(HELP, Bool, '\0', "help", "Display help.");

REGISTER_BUILTIN_FLAGS(Bg);

namespace koshka {

static fn resume_job_in_background(ExecContext &ec, EvalContext &cxt,
                                   job *job) throws -> void
{
  LOG(Info, "bg resuming job %d in the background", job->id);
  continue_job(*job);
  ec.print_to_stdout("[" + String::from(job->id, cxt.scratch_allocator()) +
                     "] " + job->command + " &\n");
}

fn Bg::execute(ExecContext &ec, EvalContext &cxt) const throws -> i32
{
  let const &args = ec.args();
  ASSERT(!args.is_empty());

  cxt.job_table_store().forget_waited_jobs();

  if (args.count() <= 1) {
    job *job = cxt.job_table_store().most_recent_job();
    if (job == nullptr)
      throw ErrorWithDetails{"There is no such job", "List jobs with `jobs`"};

    resume_job_in_background(ec, cxt, job);
    return 0;
  }

  for (usize arg_position = 1; arg_position < args.count(); arg_position++) {
    job *job = cxt.job_table_store().find_job_by_spec(args[arg_position]);
    if (job == nullptr)
      throw ErrorWithDetails{"There is no such job", "List jobs with `jobs`"};

    resume_job_in_background(ec, cxt, job);
  }

  return 0;
}

}
