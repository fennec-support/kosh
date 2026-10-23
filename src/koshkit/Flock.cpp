/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the flock utility. It acquires a process lock, runs a
 * command while the lock is held, and can transfer lock ownership to a
 * transaction keeper.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../Platform.hpp"

KOSHKIT_UTIL_DECL(
    "[--transaction-held-lock] directory command [argument ...]",
    "The flock utility can run a command while holding a directory lock.");

FLAG(TRANSACTION_HELD_LOCK,
     Bool,
     '\0',
     "transaction-held-lock",
     "Keep the lock after the process that launched the transaction exits.");

REGISTER_KOSHKIT_UTIL_FLAGS(Flock);

namespace koshka::koshkit {

/* memory: the keeper and command arrays are in the scratch arena and are
           released with it.
   time:   acquire_process_lock() blocks in flock(LOCK_EX) until the lock on
           operands[0] is free. run_measured() then waits for the command to
           exit. with --transaction-held-lock, a keeper child takes the lock,
           and this process waits for that child instead. */
fn
Flock::execute(const ExecContext               &ec,
               EvalContext                     &cxt,
               const ArrayList<String>         &args,
               const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations,
                                 {.should_stop_at_first_operand = true});

  if (operands.count() < 2) return report_usage_error(ec, cxt, args[0].view());

  /* a command that cannot be found is reported here, since the spawned
     child exits with 127 and no message when exec fails. */
  let const is_command_found =
      os::has_directory_separator(operands[1].view())
          ? Path{operands[1].view()}.exists()
          : !cxt.program_resolver()
                 .search(operands[1].view(), ProgramResolver::SearchMode::First,
                         ProgramResolver::Requirement::Runnable,
                         ProgramResolver::CachePolicy::Bypass)
                 .is_empty();

  if (!is_command_found) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[1],
                            "Command '" + operands[1] + "' was not found");

    return 127;
  }

  if (FLAG_TRANSACTION_HELD_LOCK.is_enabled()) {
    let const executable = os::current_executable_path();

    if (!executable.has_value()) {
      KOSHKIT_REPORT_ERROR("Cannot find the path of the running shell");

      return 126;
    }

    let keeper = ArrayList<String>{cxt.scratch_allocator()};

    keeper.reserve(operands.count() + 7);
    keeper.push_managed(executable->view());
    keeper.push_managed("-p");
    keeper.push_managed("--mood");
    keeper.push_managed("sh");
    keeper.push_managed("-c");
    keeper.push_managed("koshkit flock \"$@\"");
    keeper.push_managed("kosh");

    for (let const &operand : operands)
      keeper.push(operand.clone());

    cxt.prepare_child_environment();

    let const result =
        os::run_measured(keeper, {}, os::measured_output::Inherit);

    if (!result.has_value()) {
      KOSHKIT_REPORT_PATH_ERROR("run", executable->view());

      return 126;
    }

    return static_cast<i32>(result->exit_status);
  }

  let const lock = os::acquire_process_lock(operands[0].view());

  if (!lock.has_value()) {
    KOSHKIT_REPORT_ERROR_AT(operand_locations[0],
                            "Cannot acquire directory lock");

    return 1;
  }

  /* at this point: lock is the held directory lock on operands[0], and the
     defer below releases it on every return path. */
  defer { os::release_process_lock(*lock); };

  let command = ArrayList<String>{cxt.scratch_allocator()};

  command.reserve(operands.count() - 1);

  for (usize position = 1; position < operands.count(); position++)
    command.push(operands[position].clone());

  cxt.prepare_child_environment();

  let const result =
      os::run_measured(command, *lock, os::measured_output::Inherit);

  if (!result.has_value()) {
    KOSHKIT_REPORT_PATH_ERROR_AT(operand_locations[1], "run", operands[1]);

    return 126;
  }

  return static_cast<i32>(result->exit_status);
}

} /* namespace koshka::koshkit */
