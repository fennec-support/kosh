/*
 *    This file is a part of the Koshka shell, (c) toiletbril, 2026
 *    See the top-level LICENSE file for the licensing information.
 *
 * This file implements the pwdx utility. It prints the working directory of
 * each process given by a process id or a /proc/PID path.
 */

#include "../CLI.hpp"
#include "../Errors.hpp"
#include "../Eval.hpp"
#include "../Koshkit.hpp"
#include "../platform/Platform.hpp"

KOSHKIT_UTIL_DECL("pid ...",
                  "The pwdx utility can print the working directory of each "
                  "process.");

REGISTER_KOSHKIT_UTIL_FLAGS(Pwdx);

namespace koshka::koshkit {

static pure fn
pwdx_process_id(StringView operand) wontthrow -> Maybe<i64>
{
  constexpr StringView PROC_PREFIX = "/proc/";

  let digits = operand;

  if (digits.starts_with(PROC_PREFIX))
    digits = digits.substring(PROC_PREFIX.length);

  if (!digits.is_all_decimal_digits()) return None;

  let const parsed = digits.to<i64>();

  if (parsed.is_error() || parsed.value() <= 0 || parsed.value() > 0x7fffffff) {
    return None;
  }

  return parsed.value();
}

fn
Pwdx::execute(const ExecContext               &ec,
              EvalContext                     &cxt,
              const ArrayList<String>         &args,
              const ArrayList<SourceLocation> &arg_locations) const throws -> i32
{
  KOSHKIT_PARSE_OPERANDS_OR_HELP(args, arg_locations);

  if (operands.is_empty()) return report_usage_error(ec, cxt, args[0].view());

  if (!os::has_process_open_file_listing()) {
    report_soft_koshkit_error(
        ec, cxt, "Process working directories are unavailable here");

    return 1;
  }

  i32 status = 0;

  for (usize operand_position = 0; operand_position < operands.count();
       operand_position++)
  {
    let const &operand    = operands[operand_position];
    let const  process_id = pwdx_process_id(operand.view());

    if (!process_id.has_value()) {
      KOSHKIT_REPORT_ERROR_AT(operand_locations[operand_position],
                              "Invalid process id '" + operand + "'",
                              "Use a positive decimal process id");
      status = 1;
      continue;
    }

    let const files =
        os::list_process_open_files(*process_id, cxt.scratch_allocator(),
                                    os::process_open_file_detail::PathsOnly);
    let const *working_directory =
        static_cast<const os::process_open_file *>(nullptr);

    for (let const &file : files) {
      if (file.use == os::process_file_use::Cwd) {
        working_directory = &file;
        break;
      }
    }

    if (working_directory == nullptr) {
      KOSHKIT_REPORT_ERROR_AT(
          operand_locations[operand_position],
          "Unable to read the working directory of process " + operand,
          "The process does not exist or belongs to another user");
      status = 1;
      continue;
    }

    let line = String{cxt.scratch_allocator(), operand.view()};

    line += ": ";
    line += working_directory->path.view();
    if (working_directory->is_deleted) line += " (deleted)";
    line.push('\n');
    ec.print_to_stdout(line.view());
  }

  return status;
}

} /* namespace koshka::koshkit */
